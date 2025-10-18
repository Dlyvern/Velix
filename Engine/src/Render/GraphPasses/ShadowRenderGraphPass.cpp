#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"

#include "Core/VulkanHelpers.hpp"
#include "Core/Shader.hpp"

#include "Engine/PushConstant.hpp"
#include "Engine/GraphicsPipelineBuilder.hpp"

#include "Engine/Components/Transform3DComponent.hpp"

#include <glm/mat4x4.hpp>

struct LightSpaceMatrixPushConstant
{
    glm::mat4 lightSpaceMatrix;
    glm::mat4 model;
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ShadowRenderGraphPass::ShadowRenderGraphPass(VkDevice device)
{
    auto queueFamilyIndices = core::VulkanContext::findQueueFamilies(core::VulkanContext::getContext()->getPhysicalDevice(), core::VulkanContext::getContext()->getSurface());
    m_commandPool = core::CommandPool::create(device, queueFamilyIndices.graphicsFamily.value());

    const VkFormat depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());

    VkAttachmentDescription depthAttachmentDescription{};
    depthAttachmentDescription.flags = 0;
    depthAttachmentDescription.format = depthFormat;
    depthAttachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // depthAttachmentDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
    subpassDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    subpassDependency.srcAccessMask = 0;
    subpassDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    subpassDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    m_renderPass = core::RenderPass::create(device, {depthAttachmentDescription}, {subpassDescription}, {subpassDependency});

    m_depthImage = core::Image<core::ImageDeleter>::create(device, core::VulkanContext::getContext()->getPhysicalDevice(), m_width, m_height, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    depthFormat);

    m_depthImage->insertImageMemoryBarrier(
        0,                                      
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
        VK_IMAGE_LAYOUT_UNDEFINED,              
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,      
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
        m_commandPool,
        core::VulkanContext::getContext()->getGraphicsQueue()
    );

    VkImageViewCreateInfo imageViewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    imageViewCI.format = depthFormat;
    imageViewCI.image = m_depthImage->vk();
    imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCI.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    imageViewCI.subresourceRange.baseMipLevel = 0;
    imageViewCI.subresourceRange.levelCount = 1;
    imageViewCI.subresourceRange.baseArrayLayer = 0;
    imageViewCI.subresourceRange.layerCount = 1;

    if(vkCreateImageView(device, &imageViewCI, nullptr, &m_depthImageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create image view");

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if(vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create a sample");

    std::vector<VkImageView> attachments{m_depthImageView};

    VkFramebufferCreateInfo framebufferCI{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebufferCI.renderPass = m_renderPass->vk();
    framebufferCI.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferCI.width = m_width;
    framebufferCI.height = m_height;
    framebufferCI.pAttachments = attachments.data();
    framebufferCI.layers = 1;

    if(vkCreateFramebuffer(device, &framebufferCI, nullptr, &m_framebuffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to create framebuffer");

    m_clearValue.depthStencil = {1.0f, 0};

    core::Shader shader("./resources/shaders/static_mesh_shadow.vert.spv", "./resources/shaders/empty.frag.spv");

    const std::vector<VkPushConstantRange> pushConstants
    {
        PushConstant<LightSpaceMatrixPushConstant>::getRange(VK_SHADER_STAGE_VERTEX_BIT)
    };
    
    const std::vector<core::DescriptorSetLayout::SharedPtr> setLayouts
    {

    };

    m_pipelineLayout = core::PipelineLayout::create(device, setLayouts, pushConstants);

    VkVertexInputBindingDescription bindingDescription;
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(glm::vec3);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attributeDescription(1);
    attributeDescription[0].binding = 0;
    attributeDescription[0].location = 0;
    attributeDescription[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription[0].offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescription.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescription.data();

    VkViewport viewport{};
    viewport.height = static_cast<float>(m_height);
    viewport.width = static_cast<float>(m_width);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    viewport.x = 0.0f;
    viewport.y = 0.0f;

    VkRect2D scissor{};
    scissor.extent = {m_width, m_height};
    scissor.offset = {0, 0};

    engine::GraphicsPipelineBuilder graphicsPipelineBuilder;
    graphicsPipelineBuilder.layout = m_pipelineLayout->vk();
    graphicsPipelineBuilder.renderPass = m_renderPass->vk();
    graphicsPipelineBuilder.viewportState.pViewports = &viewport;
    graphicsPipelineBuilder.viewportState.pScissors = &scissor;
    graphicsPipelineBuilder.shaderStages = shader.getShaderStages();

    graphicsPipelineBuilder.depthStencil.depthTestEnable = VK_TRUE;
    graphicsPipelineBuilder.depthStencil.depthWriteEnable = VK_TRUE;
    graphicsPipelineBuilder.depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    graphicsPipelineBuilder.rasterizer.depthBiasEnable = VK_TRUE;
    graphicsPipelineBuilder.rasterizer.depthBiasConstantFactor = 1.25f;
    graphicsPipelineBuilder.rasterizer.depthBiasSlopeFactor = 1.75f;
    graphicsPipelineBuilder.rasterizer.depthBiasClamp = 0.0f;

    // graphicsPipelineBuilder.dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
    // graphicsPipelineBuilder.dynamicState.dynamicStateCount = static_cast<uint32_t>(graphicsPipelineBuilder.dynamicStates.size());
    // graphicsPipelineBuilder.dynamicState.pDynamicStates = graphicsPipelineBuilder.dynamicStates.data();

    // graphicsPipelineBuilder.rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    // graphicsPipelineBuilder.rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;

    m_graphicsPipeline = graphicsPipelineBuilder.build(device, vertexInputInfo);
}

void ShadowRenderGraphPass::endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_depthImage->vk();
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
        1, &barrier
    );
}


void ShadowRenderGraphPass::setup(RenderGraphPassRecourceBuilder& graphPassBuilder)
{
}

void ShadowRenderGraphPass::compile(RenderGraphPassResourceHash& storage)
{

}

void ShadowRenderGraphPass::execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData& data)
{
    VkViewport viewport{};
    viewport.height = static_cast<float>(m_height);
    viewport.width = static_cast<float>(m_width);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    viewport.x = 0.0f;
    viewport.y = 0.0f;

    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = {m_width, m_height};
    scissor.offset = {0, 0};

    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline->vk());

    for(const auto [entity, mesh] : data.transformationBasedOnMesh)
    {
        VkBuffer vertexBuffers[] = {mesh->vertexBuffer->vkBuffer()};

        VkDeviceSize offset[] = {0};
        vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);
        vkCmdBindIndexBuffer(commandBuffer->vk(), mesh->indexBuffer->vkBuffer(), 0, mesh->indexType);

        LightSpaceMatrixPushConstant lightSpaceMatrixPushConstant;
        lightSpaceMatrixPushConstant.lightSpaceMatrix = data.lightSpaceMatrix;
        lightSpaceMatrixPushConstant.model = glm::mat4(1.0);

        if(auto tr = entity->getComponent<Transform3DComponent>())
            lightSpaceMatrixPushConstant.model = tr->getMatrix();
        else
            std::cerr << "ERROR: MODEL DOES NOT HAVE TRANSFORM COMPONENT. SOMETHING IS WEIRD" << std::endl;

        vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(LightSpaceMatrixPushConstant), &lightSpaceMatrixPushConstant);
        // vkCmdSetDepthBias(commandBuffer->vk(), 2.0f, 0.0f, 4.0f);
        vkCmdDrawIndexed(commandBuffer->vk(), mesh->indicesCount, 1, 0, 0, 0);
    }
}

void ShadowRenderGraphPass::update(const RenderGraphPassContext& renderData)
{

}

void ShadowRenderGraphPass::getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const
{
    renderPassBeginInfo = VkRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass = m_renderPass->vk();
    renderPassBeginInfo.framebuffer = m_framebuffer;
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = VkExtent2D{m_width, m_height};
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = &m_clearValue;
}

ELIX_NESTED_NAMESPACE_END