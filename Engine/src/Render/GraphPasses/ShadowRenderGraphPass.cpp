#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"

#include "Core/VulkanHelpers.hpp"
#include "Core/Shader.hpp"

#include "Engine/PushConstant.hpp"

#include "Engine/Components/Transform3DComponent.hpp"

#include <glm/mat4x4.hpp>

struct LightSpaceMatrixPushConstant
{
    glm::mat4 lightSpaceMatrix;
    glm::mat4 model;
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

ShadowRenderGraphPass::ShadowRenderGraphPass()
{
}

void ShadowRenderGraphPass::setup(RGPResourcesBuilder &builder)
{
    const VkFormat depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());

    RGPTextureDescription depthTextureDescription{};
    depthTextureDescription.setDebugName("__ELIX_SHADOW_DEPTH_TEXTURE__");
    depthTextureDescription.setExtent(VkExtent2D{.width = m_width, .height = m_height});
    depthTextureDescription.setFormat(depthFormat);
    depthTextureDescription.setUsage(RGPTextureUsage::DEPTH_STENCIL);
    depthTextureDescription.setIsSwapChainTarget(false);

    m_depthTextureHandler = builder.createTexture(depthTextureDescription);
    builder.write(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);

    auto device = core::VulkanContext::getContext()->getDevice();

    VkAttachmentDescription depthAttachmentDescription{};
    depthAttachmentDescription.format = depthFormat;
    depthAttachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentReference{};
    depthAttachmentReference.attachment = 0;
    depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassDescription{};
    subpassDescription.colorAttachmentCount = 0;
    subpassDescription.pColorAttachments = nullptr;
    subpassDescription.flags = 0;
    subpassDescription.pDepthStencilAttachment = &depthAttachmentReference;

    VkSubpassDependency subpassDependency{};
    subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDependency.dstSubpass = 0;
    subpassDependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    subpassDependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    subpassDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    subpassDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    subpassDependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    m_renderPass = core::RenderPass::create({depthAttachmentDescription}, {subpassDescription}, {subpassDependency});

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create a sample");

    m_clearValue.depthStencil = {1.0f, 0};

    m_pipelineLayout = core::PipelineLayout::createShared(device, std::vector<core::DescriptorSetLayout::SharedPtr>{}, std::vector<VkPushConstantRange>{PushConstant<LightSpaceMatrixPushConstant>::getRange(VK_SHADER_STAGE_VERTEX_BIT)});

    m_viewport.height = static_cast<float>(m_height);
    m_viewport.width = static_cast<float>(m_width);
    m_viewport.minDepth = 0.0f;
    m_viewport.maxDepth = 1.0f;
    m_viewport.x = 0.0f;
    m_viewport.y = 0.0f;

    m_scissor.extent = {m_width, m_height};
    m_scissor.offset = {0, 0};

    //-------
    auto shader = core::Shader::create("./resources/shaders/static_mesh_shadow.vert.spv",
                                       "./resources/shaders/empty.frag.spv");

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    std::vector<VkDynamicState> dynamicStates;

    std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    uint32_t subpass = 0;

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 4.0f;
    rasterizer.depthBiasSlopeFactor = 4.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;

    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {};
    depthStencil.back = {};

    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;
    colorBlending.pAttachments = nullptr;

    VkViewport viewport = m_viewport;
    VkRect2D scissor = m_scissor;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.pScissors = &scissor;

    dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
    dynamicState.pDynamicStates = dynamicStates.data();

    shaderStages = shader->getShaderStages();

    vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::Vertex3D))};
    vertexAttributeDescriptions = {vertex::Vertex3D::getAttributeDescriptions()};

    VkPipelineVertexInputStateCreateInfo vertexInputStateCI{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputStateCI.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindingDescriptions.size());
    vertexInputStateCI.pVertexBindingDescriptions = vertexBindingDescriptions.data();
    vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescriptions.size());
    vertexInputStateCI.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

    m_graphicsPipeline = std::make_shared<core::GraphicsPipeline>(device, m_renderPass->vk(), shaderStages.data(), static_cast<uint32_t>(shaderStages.size()),
                                                                  m_pipelineLayout->vk(), dynamicState, colorBlending, multisampling, rasterizer, viewportState,
                                                                  inputAssembly, vertexInputStateCI, subpass, depthStencil);
}

void ShadowRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    m_renderTarget = storage.getTexture(m_depthTextureHandler);

    m_renderTarget->getImage()->insertImageMemoryBarrier(
        0,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1});

    std::vector<VkImageView> attachments{m_renderTarget->vkImageView()};

    m_framebuffer = std::make_shared<core::Framebuffer>(core::VulkanContext::getContext()->getDevice(), attachments,
                                                        m_renderPass, VkExtent2D{.width = m_width, .height = m_height});
}

void ShadowRenderGraphPass::endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_renderTarget->getImage()->vk();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer->vk(),
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

void ShadowRenderGraphPass::execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetDepthBias(commandBuffer->vk(), 1.25f, 0.0f, 1.75f);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline->vk());

    for (const auto &[entity, gpuEntity] : data.meshes)
    {
        for (const auto &mesh : gpuEntity.meshes)
        {
            VkBuffer vertexBuffers[] = {mesh->vertexBuffer->vk()};
            VkDeviceSize offset[] = {0};

            vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);
            vkCmdBindIndexBuffer(commandBuffer->vk(), mesh->indexBuffer->vk(), 0, mesh->indexType);

            LightSpaceMatrixPushConstant lightSpaceMatrixPushConstant{
                .lightSpaceMatrix = data.lightSpaceMatrix,
                .model = gpuEntity.transform};

            vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(LightSpaceMatrixPushConstant), &lightSpaceMatrixPushConstant);
            vkCmdDrawIndexed(commandBuffer->vk(), mesh->indicesCount, 1, 0, 0, 0);
        }
    }
}

void ShadowRenderGraphPass::update(const RenderGraphPassContext &renderData)
{
}

void ShadowRenderGraphPass::getRenderPassBeginInfo(VkRenderPassBeginInfo &renderPassBeginInfo) const
{
    renderPassBeginInfo = VkRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass = m_renderPass->vk();
    renderPassBeginInfo.framebuffer = m_framebuffer->vk();
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = VkExtent2D{m_width, m_height};
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = &m_clearValue;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END