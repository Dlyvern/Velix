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

ShadowRenderGraphPass::ShadowRenderGraphPass(VkDevice device)
{
    m_commandPool = core::CommandPool::create(device, core::VulkanContext::getContext()->getGraphicsFamily());

    const VkFormat depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());

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

    m_depthImage = core::Image::create(device, core::VulkanContext::getContext()->getPhysicalDevice(), m_width, m_height, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
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

    m_pipelineLayout = core::PipelineLayout::create(device, {}, {PushConstant<LightSpaceMatrixPushConstant>::getRange(VK_SHADER_STAGE_VERTEX_BIT)});

    m_viewport.height = static_cast<float>(m_height);
    m_viewport.width = static_cast<float>(m_width);
    m_viewport.minDepth = 0.0f;
    m_viewport.maxDepth = 1.0f;
    m_viewport.x = 0.0f;
    m_viewport.y = 0.0f;

    m_scissor.extent = {m_width, m_height};
    m_scissor.offset = {0, 0};
}

void ShadowRenderGraphPass::endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer)
{
    // barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    // barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;


    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
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
    RenderGraphPassResourceTypes::SizeSpec sizeSpec
    {
        .type = RenderGraphPassResourceTypes::SizeClass::Custom,
        .width = m_width,
        .height = m_height,
    };

    RenderGraphPassResourceTypes::TextureDescription depthImage{
        .name = "__ELIX_SHADOW_DEPTH__",
        .format = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice()),
        .size = sizeSpec,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
        .properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
    };

    m_depthImageHash = graphPassBuilder.createTexture(depthImage, {.access = ResourceAccess::WRITE, .user = this});

    const std::string name = "__ELIX_SHADOW_FRAMEBUFFER__";

    RenderGraphPassResourceTypes::FramebufferDescription framebufferDescription{
        .name = name,
        .attachmentsHash = {m_depthImageHash},
        .renderPass = m_renderPass,
        // .renderPassHash = m_renderPassHash,
        .size = sizeSpec,
        .layers = 1
    };

    m_framebufferHash = graphPassBuilder.createFramebuffer(framebufferDescription);

    auto shader = core::Shader::create("./resources/shaders/static_mesh_shadow.vert.spv",
    "./resources/shaders/empty.frag.spv");

    RenderGraphPassResourceTypes::GraphicsPipelineDescription graphicsPipeline
    {
        .name = "__ELIX_SHADOW_GRAPHICS_PIPELINE__",
        .vertexBindingDescriptions = {Vertex3D::getBindingDescription()},
        .vertexAttributeDescriptions = {Vertex3D::getAttributeDescriptions()},
        .layout = m_pipelineLayout->vk(),
        .renderPass = m_renderPass->vk(),
    };

    graphicsPipeline.dynamicStates = 
    {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BIAS
    };

    graphicsPipeline.viewport = m_viewport;
    graphicsPipeline.scissor= m_scissor;
    graphicsPipeline.shader = shader;

    graphicsPipeline.rasterizer.depthBiasEnable = VK_TRUE;
    graphicsPipeline.rasterizer.depthBiasConstantFactor = 1.25f;
    graphicsPipeline.rasterizer.depthBiasSlopeFactor = 1.75f;
    graphicsPipeline.rasterizer.depthBiasClamp = 0.0f;
    graphicsPipeline.rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;

    graphicsPipeline.depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    m_graphicsPipelineHash = graphPassBuilder.createGraphicsPipeline(graphicsPipeline);
}

void ShadowRenderGraphPass::compile(RenderGraphPassResourceHash& storage)
{
    m_graphicsPipeline = storage.getGraphicsPipeline(m_graphicsPipelineHash);

    // m_depthImage = storage.getTexture(m_depthImageHash);

    // m_depthImage->insertImageMemoryBarrier(
    //     0,                                      
    //     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
    //     VK_IMAGE_LAYOUT_UNDEFINED,              
    //     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    //     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,      
    //     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
    //     {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
    //     m_commandPool,
    //     core::VulkanContext::getContext()->getGraphicsQueue()
    // );

}

void ShadowRenderGraphPass::execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData& data)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetDepthBias(commandBuffer->vk(), 1.25f, 0.0f, 1.75f);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline->vk());

    for(const auto& [entity, gpuEntity] : data.meshes)
    {
        for(const auto& mesh : gpuEntity.meshes)
        {
            VkBuffer vertexBuffers[] = {mesh->vertexBuffer->vkBuffer()};
            VkDeviceSize offset[] = {0};

            vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);
            vkCmdBindIndexBuffer(commandBuffer->vk(), mesh->indexBuffer->vkBuffer(), 0, mesh->indexType);

            LightSpaceMatrixPushConstant lightSpaceMatrixPushConstant
            {
                .lightSpaceMatrix = data.lightSpaceMatrix,
                .model = gpuEntity.transform
            };

            vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(LightSpaceMatrixPushConstant), &lightSpaceMatrixPushConstant);
            vkCmdDrawIndexed(commandBuffer->vk(), mesh->indicesCount, 1, 0, 0, 0);
        }
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