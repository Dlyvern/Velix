#include "Engine/Render/GraphPasses/DecalRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Components/DecalComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
    struct DecalPC
    {
        glm::mat4 worldViewProj; // 64 bytes
        glm::mat4 worldToLocal;  // 64 bytes
        glm::vec4 params;        // x=opacity, y=invW, z=invH, w=blendMode – 16 bytes
    };
    static_assert(sizeof(DecalPC) == 144, "DecalPC must be 144 bytes");

    // Unit cube: [-0.5, 0.5]^3, position-only vertices (vec3)
    static const float s_cubeVerts[24] = {
        -0.5f, -0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,
         0.5f,  0.5f, -0.5f,
        -0.5f,  0.5f, -0.5f,
        -0.5f, -0.5f,  0.5f,
         0.5f, -0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,
        -0.5f,  0.5f,  0.5f,
    };

    // 12 triangles (36 indices), CCW winding when viewed from outside
    static const uint32_t s_cubeIndices[36] = {
        0, 2, 1,  0, 3, 2,  // -Z
        4, 5, 6,  4, 6, 7,  // +Z
        0, 4, 7,  0, 7, 3,  // -X
        1, 2, 6,  1, 6, 5,  // +X
        0, 1, 5,  0, 5, 4,  // -Y
        3, 7, 6,  3, 6, 2,  // +Y
    };
}

DecalRenderGraphPass::DecalRenderGraphPass(std::vector<RGPResourceHandler> &albedoHandlers,
                                           std::vector<RGPResourceHandler> &normalHandlers,
                                           std::vector<RGPResourceHandler> &materialHandlers,
                                           std::vector<RGPResourceHandler> &emissiveHandlers,
                                           RGPResourceHandler              &depthHandler)
    : m_albedoHandlers(albedoHandlers)
    , m_normalHandlers(normalHandlers)
    , m_materialHandlers(materialHandlers)
    , m_emissiveHandlers(emissiveHandlers)
    , m_depthHandler(depthHandler)
{
    setDebugName("Decal render graph pass");
    m_extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();
}

bool DecalRenderGraphPass::isEnabled() const
{
    return RenderQualitySettings::getInstance().enableDecals;
}

void DecalRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    // Read depth for world-position reconstruction
    builder.read(m_depthHandler, RGPTextureUsage::SAMPLED);

    // Write to GBuffer channels (LOAD_OP_LOAD to preserve existing content)
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        builder.write(m_albedoHandlers[i],   RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(m_normalHandlers[i],   RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(m_materialHandlers[i], RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(m_emissiveHandlers[i], RGPTextureUsage::COLOR_ATTACHMENT);
    }

    auto device = core::VulkanContext::getContext()->getDevice();

    // Set 1 layout: just the depth sampler
    auto makeBinding = [](uint32_t binding, VkDescriptorType type) -> VkDescriptorSetLayoutBinding
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding        = binding;
        b.descriptorType = type;
        b.descriptorCount = 1;
        b.stageFlags     = VK_SHADER_STAGE_FRAGMENT_BIT;
        return b;
    };

    m_depthSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{
            makeBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
            *EngineShaderFamilies::cameraDescriptorSetLayout,
            *m_depthSetLayout,
            *EngineShaderFamilies::materialDescriptorSetLayout},
        std::vector<VkPushConstantRange>{
            {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DecalPC)}});

    m_depthSampler = core::Sampler::createShared(
        VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void DecalRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    m_albedoTargets.resize(imageCount);
    m_normalTargets.resize(imageCount);
    m_materialTargets.resize(imageCount);
    m_emissiveTargets.resize(imageCount);

    m_depthTarget = storage.getTexture(m_depthHandler);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_albedoTargets[i]   = storage.getTexture(m_albedoHandlers[i]);
        m_normalTargets[i]   = storage.getTexture(m_normalHandlers[i]);
        m_materialTargets[i] = storage.getTexture(m_materialHandlers[i]);
        m_emissiveTargets[i] = storage.getTexture(m_emissiveHandlers[i]);
    }

    // Build per-frame depth descriptor sets
    m_depthDescriptorSets.resize(imageCount, VK_NULL_HANDLE);
    if (!m_descriptorSetsInitialized)
    {
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            m_depthDescriptorSets[i] = DescriptorSetBuilder::begin()
                .addImage(m_depthTarget->vkImageView(), m_depthSampler,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 0)
                .build(core::VulkanContext::getContext()->getDevice(),
                       core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                       m_depthSetLayout->vk());
        }
        m_descriptorSetsInitialized = true;
    }
    else
    {
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            DescriptorSetBuilder::begin()
                .addImage(m_depthTarget->vkImageView(), m_depthSampler,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 0)
                .update(core::VulkanContext::getContext()->getDevice(), m_depthDescriptorSets[i]);
        }
    }

    // Unit cube vertex + index buffers (created once)
    if (!m_cubeVB)
    {
        m_cubeVB = core::Buffer::createShared(
            sizeof(s_cubeVerts),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU);
        m_cubeVB->upload(s_cubeVerts, sizeof(s_cubeVerts));
    }
    if (!m_cubeIB)
    {
        m_cubeIB = core::Buffer::createShared(
            sizeof(s_cubeIndices),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU);
        m_cubeIB->upload(s_cubeIndices, sizeof(s_cubeIndices));
    }
}

void DecalRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                  const RenderGraphPassPerFrameData &data,
                                  const RenderGraphPassContext &renderContext)
{
    if (!m_scene || !m_pipelineLayout)
        return;

    VkCommandBuffer cmd = commandBuffer->vk();

    // Collect and sort decal entities
    struct DecalEntry
    {
        DecalComponent     *component;
        glm::mat4           worldMatrix;
        int                 sortOrder;
    };
    std::vector<DecalEntry> decals;

    for (const auto &entity : m_scene->getEntities())
    {
        auto *decal = entity->getComponent<DecalComponent>();
        if (!decal || !decal->material)
            continue;
        if (decal->material->getDomain() != MaterialDomain::DeferredDecal)
            continue;

        auto *transform = entity->getComponent<Transform3DComponent>();
        glm::mat4 worldMatrix = transform ? transform->getMatrix() : glm::mat4(1.0f);
        decals.push_back({decal, worldMatrix, decal->sortOrder});
    }

    if (decals.empty())
        return;

    std::stable_sort(decals.begin(), decals.end(),
                     [](const DecalEntry &a, const DecalEntry &b) { return a.sortOrder < b.sortOrder; });

    RENDER_GRAPH_DRAW_PROFILE(cmd, "Decals");

    const uint32_t frame = renderContext.currentFrame;
    const uint32_t imgIdx = renderContext.currentImageIndex;

    const float invW = 1.0f / static_cast<float>(std::max(m_extent.width,  1u));
    const float invH = 1.0f / static_cast<float>(std::max(m_extent.height, 1u));

    // Bind camera descriptor set (set 0)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout->vk(),
                            0, 1, &data.cameraDescriptorSet, 0, nullptr);

    // Bind depth descriptor set (set 1) — same for all decals
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout->vk(),
                            1, 1, &m_depthDescriptorSets[imgIdx], 0, nullptr);

    // Cube VB/IB
    VkDeviceSize zero = 0;
    VkBuffer vb = m_cubeVB->vk();
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &zero);
    vkCmdBindIndexBuffer(cmd, m_cubeIB->vk(), 0, VK_INDEX_TYPE_UINT32);

    for (const auto &entry : decals)
    {
        auto *decal = entry.component;

        // Pipeline
        GraphicsPipelineKey key{};
        key.shader = ShaderId::Decal;
        key.blend  = BlendMode::AlphaBlend;
        key.cull   = CullMode::None;
        key.depthTest  = false;
        key.depthWrite = false;
        key.pipelineLayout = m_pipelineLayout->vk();
        key.colorFormats = {
            VK_FORMAT_R16G16B16A16_SFLOAT, // normal
            VK_FORMAT_R8G8B8A8_UNORM,      // albedo
            VK_FORMAT_R8G8B8A8_UNORM,      // material
            VK_FORMAT_R16G16B16A16_SFLOAT, // emissive
        };
        key.depthFormat = VK_FORMAT_UNDEFINED;

        auto pipeline = GraphicsPipelineManager::getOrCreate(key);
        if (!pipeline)
            continue;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vk());

        // Set 2: material textures
        VkDescriptorSet matSet = decal->material->getDescriptorSet(frame);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout->vk(),
                                2, 1, &matSet, 0, nullptr);

        // Viewport/scissor
        VkViewport vp{0, 0, (float)m_extent.width, (float)m_extent.height, 0.0f, 1.0f};
        VkRect2D   sc{{0, 0}, m_extent};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd,  0, 1, &sc);

        // Push constants
        DecalPC pc{};
        pc.worldViewProj = data.projection * data.view * entry.worldMatrix;
        pc.worldToLocal  = glm::affineInverse(entry.worldMatrix);
        pc.params = {decal->opacity, invW, invH,
                     static_cast<float>(static_cast<int>(decal->material->getDecalBlendMode()))};
        vkCmdPushConstants(cmd, m_pipelineLayout->vk(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(DecalPC), &pc);

        vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);
    }
}

std::vector<IRenderGraphPass::RenderPassExecution> DecalRenderGraphPass::getRenderPassExecutions(
    const RenderGraphPassContext &renderContext) const
{
    if (m_albedoTargets.empty())
        return {};

    const uint32_t imgIdx = renderContext.currentImageIndex;

    auto makeLoadAttachment = [](VkImageView view) -> VkRenderingAttachmentInfo
    {
        VkRenderingAttachmentInfo info{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        info.imageView   = view;
        info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        info.loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD;
        info.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        return info;
    };

    RenderPassExecution exec;
    exec.renderArea.extent = m_extent;
    exec.useDepth = false;

    // GBuffer attachment order: normal(0), albedo(1), material(2), emissive(3)
    exec.colorsRenderingItems = {
        makeLoadAttachment(m_normalTargets[imgIdx]->vkImageView()),
        makeLoadAttachment(m_albedoTargets[imgIdx]->vkImageView()),
        makeLoadAttachment(m_materialTargets[imgIdx]->vkImageView()),
        makeLoadAttachment(m_emissiveTargets[imgIdx]->vkImageView()),
    };
    exec.colorFormats = {
        VK_FORMAT_R16G16B16A16_SFLOAT, // normal
        VK_FORMAT_R8G8B8A8_UNORM,      // albedo
        VK_FORMAT_R8G8B8A8_UNORM,      // material
        VK_FORMAT_R16G16B16A16_SFLOAT, // emissive
    };

    exec.targets[m_normalHandlers[imgIdx]]   = m_normalTargets[imgIdx];
    exec.targets[m_albedoHandlers[imgIdx]]   = m_albedoTargets[imgIdx];
    exec.targets[m_materialHandlers[imgIdx]] = m_materialTargets[imgIdx];
    exec.targets[m_emissiveHandlers[imgIdx]] = m_emissiveTargets[imgIdx];
    exec.targets[m_depthHandler]             = m_depthTarget;

    return {exec};
}

void DecalRenderGraphPass::cleanup()
{
    m_pipelineLayout.reset();
    m_depthSetLayout.reset();
    m_depthSampler.reset();
    m_depthDescriptorSets.clear();
    m_cubeVB.reset();
    m_cubeIB.reset();
    m_descriptorSetsInitialized = false;
}

void DecalRenderGraphPass::freeResources()
{
    m_albedoTargets.clear();
    m_normalTargets.clear();
    m_materialTargets.clear();
    m_emissiveTargets.clear();
    m_depthTarget = nullptr;
    m_depthDescriptorSets.clear();
    m_descriptorSetsInitialized = false;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
