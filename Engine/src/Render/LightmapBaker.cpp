#include "Engine/Render/LightmapBaker.hpp"

#include "Core/Buffer.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/Logger.hpp"
#include "Core/Sampler.hpp"
#include "Core/VulkanContext.hpp"

#include "Engine/Assets/AssetsSerializer.hpp"
#include "Engine/Assets/Asset.hpp"
#include "Engine/Components/LightmapComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Entity.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"
#include "Engine/Render/MeshGeometryRegistry.hpp"
#include "Engine/Render/RenderGraph/RenderGraph.hpp"
#include "Engine/Render/RenderGraphPassPerFrameData.hpp"
#include "Engine/Scene.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_inverse.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

// ---- helpers ----------------------------------------------------------------

namespace
{
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props)
    {
        VkPhysicalDeviceMemoryProperties mem{};
        vkGetPhysicalDeviceMemoryProperties(core::VulkanContext::getContext()->getPhysicalDevice(), &mem);
        for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
            if ((typeBits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & props) == props)
                return i;
        return 0;
    }

    VkShaderModule loadShader(VkDevice device, const std::string &path)
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open())
        {
            VX_ENGINE_ERROR_STREAM("[LightmapBaker] Cannot open shader: " << path << '\n');
            return VK_NULL_HANDLE;
        }
        const size_t size = static_cast<size_t>(f.tellg());
        f.seekg(0);
        std::vector<char> code(size);
        f.read(code.data(), static_cast<std::streamsize>(size));

        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = size;
        ci.pCode    = reinterpret_cast<const uint32_t *>(code.data());

        VkShaderModule mod = VK_NULL_HANDLE;
        vkCreateShaderModule(device, &ci, nullptr, &mod);
        return mod;
    }
} // namespace

// ---- LightmapBaker ----------------------------------------------------------

LightmapBaker::~LightmapBaker()
{
    destroyPipeline();
}

void LightmapBaker::createPipeline(uint32_t resolution)
{
    if (m_pipelineInitialised && m_lastResolution == resolution)
        return;

    destroyPipeline();
    m_lastResolution = resolution;

    auto ctx     = core::VulkanContext::getContext();
    VkDevice dev = ctx->getDevice();

    // Shadow sampler — plain linear sampler, no comparison.
    // The bake shader uses sampler2DArray and does manual depth comparison,
    // so compareEnable must be VK_FALSE to get raw depth values from texture().
    m_shadowSampler = core::Sampler::createShared(
        VK_FILTER_LINEAR,                        // magFilter
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, // addressModeU
        VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,      // borderColor
        VK_COMPARE_OP_NEVER,                     // compareOp (unused)
        VK_SAMPLER_MIPMAP_MODE_LINEAR,           // mipmapMode
        VK_FALSE,                                // anisotropyEnable
        1.0f,                                    // maxAnisotropy
        VK_FALSE,                                // unnormalizedCoordinates
        VK_FALSE                                 // compareEnable — raw depth readback
    );

    // Render pass — single R16G16B16A16_SFLOAT color attachment, no depth.
    VkAttachmentDescription colorAtt{};
    colorAtt.format         = VK_FORMAT_R16G16B16A16_SFLOAT;
    colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpCI{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpCI.attachmentCount = 1;
    rpCI.pAttachments    = &colorAtt;
    rpCI.subpassCount    = 1;
    rpCI.pSubpasses      = &subpass;
    rpCI.dependencyCount = 1;
    rpCI.pDependencies   = &dep;
    vkCreateRenderPass(dev, &rpCI, nullptr, &m_bakeRenderPass);

    // Descriptor set layout for set 1 (shadow maps)
    const std::array<VkDescriptorSetLayoutBinding, 3> bindings{{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    }};
    VkDescriptorSetLayoutCreateInfo dslCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslCI.bindingCount = static_cast<uint32_t>(bindings.size());
    dslCI.pBindings    = bindings.data();
    vkCreateDescriptorSetLayout(dev, &dslCI, nullptr, &m_bakeSetLayout);

    // Descriptor pool (1 set per baker lifetime)
    const std::array<VkDescriptorPoolSize, 1> poolSizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3}}};
    VkDescriptorPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolCI.maxSets       = 1;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes    = poolSizes.data();
    vkCreateDescriptorPool(dev, &poolCI, nullptr, &m_bakeDescriptorPool);

    // Push constant: model mat4 + normalMatrix mat4 = 128 bytes
    VkPushConstantRange pcRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) * 2};

    // We need set 0 layout from the engine (camera descriptor layout).
    // Retrieve it from EngineShaderFamilies.
    VkDescriptorSetLayout set0Layout = EngineShaderFamilies::cameraDescriptorSetLayout;

    const std::array<VkDescriptorSetLayout, 2> layouts{set0Layout, m_bakeSetLayout};
    VkPipelineLayoutCreateInfo layoutCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutCI.setLayoutCount         = 2;
    layoutCI.pSetLayouts            = layouts.data();
    layoutCI.pushConstantRangeCount = 1;
    layoutCI.pPushConstantRanges    = &pcRange;
    vkCreatePipelineLayout(dev, &layoutCI, nullptr, &m_bakePipelineLayout);

    // Load shaders
    VkShaderModule vertMod = loadShader(dev, "resources/shaders/lightmap_bake.vert.spv");
    VkShaderModule fragMod = loadShader(dev, "resources/shaders/lightmap_bake.frag.spv");
    if (vertMod == VK_NULL_HANDLE || fragMod == VK_NULL_HANDLE)
    {
        VX_ENGINE_ERROR_STREAM("[LightmapBaker] Failed to load bake shaders.\n");
        return;
    }

    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_VERTEX_BIT,   vertMod, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fragMod, "main", nullptr},
    }};

    // Vertex input: binding 0 = Vertex3D, binding 1 = lightmapUV (vec2)
    const std::array<VkVertexInputBindingDescription, 2> bindings2{{
        {0, sizeof(vertex::Vertex3D), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(float) * 2,        VK_VERTEX_INPUT_RATE_VERTEX},
    }};
    const std::array<VkVertexInputAttributeDescription, 6> attrs{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex::Vertex3D, position)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(vertex::Vertex3D, textureCoordinates)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex::Vertex3D, normal)},
        {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex::Vertex3D, bitangent)},
        {4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex::Vertex3D, tangent)},
        {5, 1, VK_FORMAT_R32G32_SFLOAT,    0},
    }};
    VkPipelineVertexInputStateCreateInfo viCI{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    viCI.vertexBindingDescriptionCount   = 2;
    viCI.pVertexBindingDescriptions      = bindings2.data();
    viCI.vertexAttributeDescriptionCount = 6;
    viCI.pVertexAttributeDescriptions    = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo iaCI{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    iaCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{0, 0, static_cast<float>(resolution), static_cast<float>(resolution), 0, 1};
    VkRect2D   sc{{0, 0}, {resolution, resolution}};
    VkPipelineViewportStateCreateInfo vpCI{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vpCI.viewportCount = 1; vpCI.pViewports = &vp;
    vpCI.scissorCount  = 1; vpCI.pScissors  = &sc;

    VkPipelineRasterizationStateCreateInfo rCI{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rCI.polygonMode = VK_POLYGON_MODE_FILL;
    rCI.cullMode    = VK_CULL_MODE_NONE; // bake both faces
    rCI.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rCI.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo msCI{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    msCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cbCI{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cbCI.attachmentCount = 1; cbCI.pAttachments = &blendAtt;

    VkGraphicsPipelineCreateInfo gpCI{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpCI.stageCount          = 2;
    gpCI.pStages             = stages.data();
    gpCI.pVertexInputState   = &viCI;
    gpCI.pInputAssemblyState = &iaCI;
    gpCI.pViewportState      = &vpCI;
    gpCI.pRasterizationState = &rCI;
    gpCI.pMultisampleState   = &msCI;
    gpCI.pColorBlendState    = &cbCI;
    gpCI.layout              = m_bakePipelineLayout;
    gpCI.renderPass          = m_bakeRenderPass;
    gpCI.subpass             = 0;
    vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpCI, nullptr, &m_bakePipeline);

    vkDestroyShaderModule(dev, vertMod, nullptr);
    vkDestroyShaderModule(dev, fragMod, nullptr);

    m_pipelineInitialised = true;
}

void LightmapBaker::destroyPipeline()
{
    if (!m_pipelineInitialised)
        return;

    VkDevice dev = core::VulkanContext::getContext()->getDevice();
    vkDeviceWaitIdle(dev);

    if (m_shadowDescriptorSet != VK_NULL_HANDLE)
        m_shadowDescriptorSet = VK_NULL_HANDLE; // freed by pool

    if (m_bakeDescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(dev, m_bakeDescriptorPool, nullptr);
        m_bakeDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_bakeSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(dev, m_bakeSetLayout, nullptr);
        m_bakeSetLayout = VK_NULL_HANDLE;
    }
    if (m_bakePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(dev, m_bakePipeline, nullptr);
        m_bakePipeline = VK_NULL_HANDLE;
    }
    if (m_bakePipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(dev, m_bakePipelineLayout, nullptr);
        m_bakePipelineLayout = VK_NULL_HANDLE;
    }
    if (m_bakeRenderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(dev, m_bakeRenderPass, nullptr);
        m_bakeRenderPass = VK_NULL_HANDLE;
    }
    m_shadowSampler.reset();
    m_pipelineInitialised = false;
}

LightmapBaker::BakeTarget LightmapBaker::createBakeTarget(uint32_t resolution)
{
    BakeTarget t{};
    VkDevice dev = core::VulkanContext::getContext()->getDevice();

    VkImageCreateInfo imgCI{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imgCI.imageType     = VK_IMAGE_TYPE_2D;
    imgCI.format        = VK_FORMAT_R16G16B16A16_SFLOAT;
    imgCI.extent        = {resolution, resolution, 1};
    imgCI.mipLevels     = 1;
    imgCI.arrayLayers   = 1;
    imgCI.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imgCI.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(dev, &imgCI, nullptr, &t.image);

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(dev, t.image, &mr);
    VkMemoryAllocateInfo allocCI{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocCI.allocationSize  = mr.size;
    allocCI.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(dev, &allocCI, nullptr, &t.memory);
    vkBindImageMemory(dev, t.image, t.memory, 0);

    VkImageViewCreateInfo ivCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivCI.image                           = t.image;
    ivCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    ivCI.format                          = VK_FORMAT_R16G16B16A16_SFLOAT;
    ivCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ivCI.subresourceRange.levelCount     = 1;
    ivCI.subresourceRange.layerCount     = 1;
    vkCreateImageView(dev, &ivCI, nullptr, &t.imageView);

    VkFramebufferCreateInfo fbCI{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbCI.renderPass      = m_bakeRenderPass;
    fbCI.attachmentCount = 1;
    fbCI.pAttachments    = &t.imageView;
    fbCI.width           = resolution;
    fbCI.height          = resolution;
    fbCI.layers          = 1;
    vkCreateFramebuffer(dev, &fbCI, nullptr, &t.framebuffer);

    return t;
}

void LightmapBaker::destroyBakeTarget(BakeTarget &t)
{
    VkDevice dev = core::VulkanContext::getContext()->getDevice();
    if (t.framebuffer) { vkDestroyFramebuffer(dev, t.framebuffer, nullptr); t.framebuffer = VK_NULL_HANDLE; }
    if (t.imageView)   { vkDestroyImageView(dev, t.imageView, nullptr);   t.imageView   = VK_NULL_HANDLE; }
    if (t.image)       { vkDestroyImage(dev, t.image, nullptr);           t.image       = VK_NULL_HANDLE; }
    if (t.memory)      { vkFreeMemory(dev, t.memory, nullptr);            t.memory      = VK_NULL_HANDLE; }
}

std::vector<uint8_t> LightmapBaker::readbackImage(VkImage image, uint32_t width, uint32_t height)
{
    // R16G16B16A16_SFLOAT = 8 bytes per pixel
    const VkDeviceSize dataSize = static_cast<VkDeviceSize>(width) * height * 8;

    auto staging = core::Buffer::createShared(dataSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                              core::memory::MemoryUsage::CPU_ONLY);

    auto cmd = core::CommandBuffer::createShared(*core::VulkanContext::getContext()->getGraphicsCommandPool());
    cmd->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    // Transition from TRANSFER_SRC_OPTIMAL (set by render pass finalLayout) → copy → back
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask               = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask               = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout                   = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image                       = image;
    barrier.subresourceRange            = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(*cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent      = {width, height, 1};
    vkCmdCopyImageToBuffer(*cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, *staging, 1, &region);

    cmd->end();
    {
        const VkQueue gfxQueue = core::VulkanContext::getContext()->getGraphicsQueue();
        cmd->submit(gfxQueue);
        vkQueueWaitIdle(gfxQueue);
    }

    std::vector<uint8_t> pixels(dataSize);
    void *mapped = nullptr;
    staging->map(mapped);
    if (mapped)
        std::memcpy(pixels.data(), mapped, dataSize);
    staging->unmap();
    return pixels;
}

void LightmapBaker::dilate(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height, uint32_t radius)
{
    if (radius == 0)
        return;

    // Each pixel is 8 bytes (R16G16B16A16_SFLOAT, stored as half-floats).
    const uint32_t stride = 8u;
    std::vector<uint8_t> result = pixels;

    auto isBlack = [&](uint32_t x, uint32_t y) -> bool {
        const uint8_t *p = pixels.data() + (y * width + x) * stride;
        // Check alpha channel (bytes 6-7) — non-zero alpha means the texel was written.
        return p[6] == 0 && p[7] == 0;
    };

    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            if (!isBlack(x, y))
                continue; // already has data

            // Find nearest non-black neighbour within radius.
            for (uint32_t r = 1; r <= radius; ++r)
            {
                bool found = false;
                for (int dy = -static_cast<int>(r); dy <= static_cast<int>(r) && !found; ++dy)
                {
                    for (int dx = -static_cast<int>(r); dx <= static_cast<int>(r) && !found; ++dx)
                    {
                        if (std::abs(dx) != static_cast<int>(r) && std::abs(dy) != static_cast<int>(r))
                            continue;
                        const int nx = static_cast<int>(x) + dx;
                        const int ny = static_cast<int>(y) + dy;
                        if (nx < 0 || ny < 0 || nx >= static_cast<int>(width) || ny >= static_cast<int>(height))
                            continue;
                        if (!isBlack(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny)))
                        {
                            std::copy_n(pixels.data() + (ny * static_cast<int>(width) + nx) * stride,
                                        stride,
                                        result.data() + (y * width + x) * stride);
                            found = true;
                        }
                    }
                }
                if (found)
                    break;
            }
        }
    }
    pixels = std::move(result);
}

void LightmapBaker::createShadowDescriptorSet(VkImageView dirView, VkImageView spotView,
                                               VkImageView cubeView, VkSampler shadowSampler)
{
    VkDevice dev = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool     = m_bakeDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &m_bakeSetLayout;
    vkAllocateDescriptorSets(dev, &allocInfo, &m_shadowDescriptorSet);

    auto makeImageInfo = [](VkImageView view, VkSampler sampler, VkImageLayout layout) {
        VkDescriptorImageInfo info{};
        info.imageView   = view;
        info.sampler     = sampler;
        info.imageLayout = layout;
        return info;
    };

    const VkImageLayout depthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    std::array<VkDescriptorImageInfo, 3> imageInfos{
        makeImageInfo(dirView,  shadowSampler, depthLayout),
        makeImageInfo(spotView, shadowSampler, depthLayout),
        makeImageInfo(cubeView, shadowSampler, depthLayout),
    };
    std::array<VkWriteDescriptorSet, 3> writes{};
    for (uint32_t i = 0; i < 3; ++i)
    {
        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = m_shadowDescriptorSet;
        writes[i].dstBinding      = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo      = &imageInfos[i];
    }
    vkUpdateDescriptorSets(dev, 3, writes.data(), 0, nullptr);
}

// ---- main bake entry --------------------------------------------------------

void LightmapBaker::bake(engine::Scene *scene,
                          renderGraph::ShadowRenderGraphPass *shadowPass,
                          renderGraph::RenderGraph *renderGraph,
                          const std::string &outputDir,
                          const LightmapBakeSettings &settings,
                          LightmapBakeProgress &progress)
{
    if (!scene || !shadowPass || !renderGraph)
        return;

    auto ctx     = core::VulkanContext::getContext();
    VkDevice dev = ctx->getDevice();
    vkDeviceWaitIdle(dev);

    std::filesystem::create_directories(outputDir);

    createPipeline(settings.resolution);
    if (!m_pipelineInitialised)
    {
        VX_ENGINE_ERROR_STREAM("[LightmapBaker] Pipeline creation failed.\n");
        return;
    }

    // Build shadow descriptor set from the shadow pass' compiled views.
    VkImageView dirView  = shadowPass->getDirectionalShadowImageView();
    VkImageView spotView = shadowPass->getSpotShadowImageView();
    VkImageView cubeView = shadowPass->getCubeShadowImageView();

    // Ensure we have valid views — fall back to tiny placeholder if not available.
    // (The baker won't crash; it just won't produce shadows for missing maps.)
    if (!dirView || !spotView || !cubeView)
    {
        VX_ENGINE_WARNING_STREAM("[LightmapBaker] One or more shadow map views are null — shadows may be missing in the lightmap.\n");
    }

    // Use the shadow sampler we created during pipeline init.
    VkSampler rawSampler = m_shadowSampler ? static_cast<VkSampler>(*m_shadowSampler) : VK_NULL_HANDLE;
    createShadowDescriptorSet(dirView  ? dirView  : VK_NULL_HANDLE,
                              spotView ? spotView : VK_NULL_HANDLE,
                              cubeView ? cubeView : VK_NULL_HANDLE,
                              rawSampler);

    // Camera descriptor set (set 0) — contains light SSBO + lightSpace UBO.
    const uint32_t imgIdx = renderGraph->getCurrentImageIndex();
    const VkDescriptorSet cameraSet = renderGraph->getCameraDescriptorSet(imgIdx);

    const auto &entities = scene->getEntities();
    progress.totalEntities.store(static_cast<uint32_t>(entities.size()));
    progress.completedEntities.store(0);
    progress.done.store(false);

    AssetsSerializer serializer;
    auto &meshRegistry = renderGraph->getMeshGeometryRegistry();

    for (const auto &entityPtr : entities)
    {
        if (!entityPtr)
            continue;

        Entity *entity = entityPtr.get();
        auto *smc = entity->getComponent<StaticMeshComponent>();
        if (!smc || smc->getMeshes().empty())
            continue;

        // Use the first mesh — each entity typically has one mesh for lightmapping.
        const CPUMesh &cpuMesh = smc->getMeshes().front();
        VX_ENGINE_INFO_STREAM("[LightmapBaker] Entity \"" << entity->getName()
                              << "\" mesh \"" << cpuMesh.name
                              << "\" lightmapUVs=" << cpuMesh.lightmapUVs.size() << '\n');
        if (cpuMesh.lightmapUVs.empty())
        {
            VX_ENGINE_WARNING_STREAM("[LightmapBaker] Skipping \"" << entity->getName()
                                     << "\" — no lightmap UVs. Re-import the FBX asset.\n");
            continue; // no lightmap UVs
        }

        auto gpuMesh = meshRegistry.getOrCreateSharedGeometryMesh(cpuMesh);
        if (!gpuMesh || !gpuMesh->lightmapUVBuffer)
            continue;

        const std::string entityName = entity->getName();
        {
            std::lock_guard<std::mutex> lock(progress.statusMutex);
            progress.currentEntityName = entityName;
        }

        const std::string assetPath = outputDir + "/" + entityName + ".texture.elixasset";

        // Build target and bake
        BakeTarget target = createBakeTarget(settings.resolution);

        // Record bake command buffer
        auto cmd = core::CommandBuffer::createShared(*ctx->getGraphicsCommandPool());
        cmd->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        VkClearValue clearVal{};
        clearVal.color = {{0, 0, 0, 0}};
        VkRenderPassBeginInfo rpBI{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpBI.renderPass        = m_bakeRenderPass;
        rpBI.framebuffer       = target.framebuffer;
        rpBI.renderArea.extent = {settings.resolution, settings.resolution};
        rpBI.clearValueCount   = 1;
        rpBI.pClearValues      = &clearVal;
        vkCmdBeginRenderPass(*cmd, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(*cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bakePipeline);

        // Set 0 = camera descriptor (lightSpace + lightSSBO)
        if (cameraSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(*cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bakePipelineLayout,
                                    0, 1, &cameraSet, 0, nullptr);

        // Set 1 = shadow maps
        if (m_shadowDescriptorSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(*cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bakePipelineLayout,
                                    1, 1, &m_shadowDescriptorSet, 0, nullptr);

        // Push constants: model + normal matrix
        struct BakePC { glm::mat4 model; glm::mat4 normal; };
        auto *tc = entity->getComponent<Transform3DComponent>();
        const glm::mat4 model = tc ? tc->getMatrix() : glm::mat4(1.0f);
        const glm::mat4 normalMat = glm::mat4(glm::inverseTranspose(glm::mat3(model)));
        const BakePC pc{model, normalMat};
        vkCmdPushConstants(*cmd, m_bakePipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(BakePC), &pc);

        // Bind vertex buffers: binding 0 = main VBO, binding 1 = lightmap UV VBO
        const std::array<VkBuffer, 2> vbs{
            gpuMesh->vertexBuffer,
            static_cast<VkBuffer>(*gpuMesh->lightmapUVBuffer),
        };
        const std::array<VkDeviceSize, 2> offsets{0, 0};
        vkCmdBindVertexBuffers(*cmd, 0, 2, vbs.data(), offsets.data());
        vkCmdBindIndexBuffer(*cmd, gpuMesh->indexBuffer, 0, gpuMesh->indexType);

        vkCmdDrawIndexed(*cmd, gpuMesh->indicesCount, 1, 0, 0, 0);

        vkCmdEndRenderPass(*cmd);
        cmd->end();

        {
            const VkQueue gfxQueue = core::VulkanContext::getContext()->getGraphicsQueue();
            cmd->submit(gfxQueue);
            vkQueueWaitIdle(gfxQueue);
        }

        // Read back and optionally dilate
        std::vector<uint8_t> pixels = readbackImage(target.image, settings.resolution, settings.resolution);
        if (settings.dilate)
            dilate(pixels, settings.resolution, settings.resolution, settings.dilateRadius);

        // Build TextureAsset and save
        TextureAsset ta;
        ta.name       = entityName + "_lightmap";
        ta.assetPath  = assetPath;
        ta.width      = settings.resolution;
        ta.height     = settings.resolution;
        ta.channels   = 4;
        ta.vkFormat   = static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT);
        ta.encoding   = TextureAsset::PixelEncoding::RGBA16F;
        ta.pixels     = std::move(pixels);

        if (!serializer.writeTexture(ta, assetPath))
        {
            VX_ENGINE_ERROR_STREAM("[LightmapBaker] Failed to write lightmap for entity: " << entityName << '\n');
        }
        else
        {
            VX_ENGINE_INFO_STREAM("[LightmapBaker] Baked: " << entityName << " → " << assetPath << '\n');

            // Assign LightmapComponent to entity
            auto *lmc = entity->getComponent<LightmapComponent>();
            if (!lmc)
                lmc = entity->addComponent<LightmapComponent>();
            lmc->loadFromPath(assetPath);
        }

        destroyBakeTarget(target);

        progress.completedEntities.fetch_add(1);
    }

    // Reset shadow descriptor set — it was allocated from the pool and will be freed when pool resets.
    m_shadowDescriptorSet = VK_NULL_HANDLE;
    // Reset pool to allow re-baking without leaking descriptors.
    if (m_bakeDescriptorPool != VK_NULL_HANDLE)
        vkResetDescriptorPool(dev, m_bakeDescriptorPool, 0);

    progress.done.store(true);
    {
        std::lock_guard<std::mutex> lock(progress.statusMutex);
        progress.currentEntityName.clear();
    }

    VX_ENGINE_INFO_STREAM("[LightmapBaker] Bake complete. "
                          << progress.completedEntities.load() << " entities processed.\n");
}

ELIX_NESTED_NAMESPACE_END
