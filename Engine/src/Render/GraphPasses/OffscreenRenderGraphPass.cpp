#include "Engine/Render/GraphPasses/OffscreenRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/VulkanHelpers.hpp"

#include "Engine/GraphicsPipelineBuilder.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Material.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include <stdexcept>

struct ModelPushConstant
{
    glm::mat4 model;
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)

OffscreenRenderGraphPass::OffscreenRenderGraphPass(core::PipelineLayout::SharedPtr pipelineLayout, const std::vector<VkDescriptorSet>& descriptorSets,
core::GraphicsPipeline::SharedPtr graphicsPipeline)
: m_pipelineLayout(pipelineLayout), m_descriptorSet(descriptorSets), m_graphicsPipeline(graphicsPipeline)
{
    const auto& context = core::VulkanContext::getContext();
    const auto& swapChain = context->getSwapchain();
    const auto& swapChainImages = swapChain->getImages();

    m_imageViews.resize(swapChainImages.size());
    m_images.reserve(swapChainImages.size());
    m_framebuffers.resize(m_imageViews.size());

    auto queueFamilyIndices = core::VulkanContext::findQueueFamilies(context->getPhysicalDevice(), context->getSurface());
    m_commandPool = core::CommandPool::create(context->getDevice(), queueFamilyIndices.graphicsFamily.value());

    m_clearValue[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    m_clearValue[1].depthStencil = {1.0f, 0};

    std::vector<VkAttachmentDescription> attachments = {};
    attachments.resize(2);
    attachments[0].format = swapChain->getImageFormat();
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1].format = core::helpers::findDepthFormat();
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference = {};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference = {};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassDescription = {};
    subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDescription.colorAttachmentCount = 1;
    subpassDescription.pColorAttachments = &colorReference;
    subpassDescription.pDepthStencilAttachment = &depthReference;
    subpassDescription.inputAttachmentCount = 0;
    subpassDescription.pInputAttachments = nullptr;
    subpassDescription.preserveAttachmentCount = 0;
    subpassDescription.pPreserveAttachments = nullptr;
    subpassDescription.pResolveAttachments = nullptr;

    // Subpass dependencies for layout transitions
    std::vector<VkSubpassDependency> dependencies;
    dependencies.resize(2);
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpassDescription;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    m_renderPass = core::RenderPass::create(context->getDevice(), attachments, {subpassDescription}, dependencies);

    createImages();
    createImageViews();
}

void OffscreenRenderGraphPass::createImages()
{
    auto swapChain = core::VulkanContext::getContext()->getSwapchain();

    for(uint32_t i = 0; i < swapChain->getImages().size(); ++i)
    {
        auto image = core::Image::create(core::VulkanContext::getContext()->getDevice(), swapChain->getExtent().width, swapChain->getExtent().height, 
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_FORMAT_B8G8R8A8_SRGB, VK_IMAGE_TILING_LINEAR);
        
        image->insertImageMemoryBarrier(VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        m_commandPool, core::VulkanContext::getContext()->getGraphicsQueue());

        m_images.push_back(image);
    }
}

void OffscreenRenderGraphPass::createImageViews()
{
    for(size_t i = 0; i < m_images.size(); ++i)
    {
        VkImageViewCreateInfo imageViewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        imageViewCI.image = m_images[i]->vk();
        imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCI.format = VK_FORMAT_B8G8R8A8_SRGB;
        imageViewCI.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCI.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCI.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCI.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewCI.subresourceRange.baseMipLevel = 0;
        imageViewCI.subresourceRange.levelCount = 1;
        imageViewCI.subresourceRange.baseArrayLayer = 0;
        imageViewCI.subresourceRange.layerCount = 1;

        if(vkCreateImageView(core::VulkanContext::getContext()->getDevice(), &imageViewCI, nullptr, &m_imageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image views");
    }
}

void OffscreenRenderGraphPass::createFramebuffers()
{
    for(size_t i = 0; i < m_imageViews.size(); ++i)
    {
        std::vector<VkImageView> attachments{m_imageViews[i], m_depthImageProxy->imageView};

        VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        // framebufferInfo.renderPass = m_viewportRenderPassProxy->storage.data->vk();
        framebufferInfo.renderPass = m_renderPass->vk();
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = core::VulkanContext::getContext()->getSwapchain()->getExtent().width;
        framebufferInfo.height = core::VulkanContext::getContext()->getSwapchain()->getExtent().height;
        framebufferInfo.layers = 1;

        if(vkCreateFramebuffer(core::VulkanContext::getContext()->getDevice(), &framebufferInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create framebuffer");
    }
}

void OffscreenRenderGraphPass::getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const
{
    renderPassBeginInfo = VkRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    // renderPassBeginInfo.renderPass = m_viewportRenderPassProxy->storage.data->vk();
    renderPassBeginInfo.renderPass = m_renderPass->vk();
    renderPassBeginInfo.framebuffer = m_framebuffers[m_imageIndex];
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();
    renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(m_clearValue.size());
    renderPassBeginInfo.pClearValues = m_clearValue.data();
}

void OffscreenRenderGraphPass::setup(RenderGraphPassBuilder::SharedPtr builder)
{
    m_depthImageProxy = builder->createProxy<ImageRenderGraphProxy>("__ELIX_SWAP_CHAIN_DEPTH_PROXY__");
    m_staticMeshProxy = builder->createProxy<StaticMeshRenderGraphProxy>("__ELIX_SCENE_STATIC_MESH_PROXY__");
    // m_viewportRenderPassProxy = builder->createProxy<RenderPassRenderGraphProxy>("__ELIX_VIEWPORT_RENDER_PASS_PROXY__");
}

void OffscreenRenderGraphPass::compile()
{
    createFramebuffers();
}

void OffscreenRenderGraphPass::execute(core::CommandBuffer::SharedPtr commandBuffer)
{
    // const auto& context = core::VulkanContext::getContext();
    // const auto& swapChain = context->getSwapchain();

    // vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline->vk());

    // auto viewport = swapChain->getViewport();
    // auto scissor = swapChain->getScissor();
    // vkCmdSetViewport(commandBuffer->vk(), 0, 1, &viewport);
    // vkCmdSetScissor(commandBuffer->vk(), 0, 1, &scissor);

    // for(const auto [enity, mesh] : m_staticMeshProxy->transformationBasedOnMesh)
    // {
    //     VkBuffer vertexBuffers[] = {mesh->vertexBuffer->vkBuffer()};

    //     VkDeviceSize offset[] = {0};
    //     vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);
    //     vkCmdBindIndexBuffer(commandBuffer->vk(), mesh->indexBuffer->vkBuffer(), 0, mesh->indexType);

    //     // getDevice()->gpuProps.limits.maxPushConstantsSize;

    //     ModelPushConstant modelPushConstant;
    //     modelPushConstant.model = glm::mat4(1.0f);

    //     if(auto tr = enity->getComponent<Transform3DComponent>())
    //         modelPushConstant.model = tr->getMatrix();
    //     else
    //         std::cerr << "ERROR: MODEL DOES NOT HAVE TRANSFORM COMPONENT. SOMETHING IS WEIRD" << std::endl;
        
    //     vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ModelPushConstant), &modelPushConstant);

    //     VkDescriptorSet materialDst = Material::getDefaultMaterial()->getDescriptorSet(m_currentFrame);

    //     if(auto staticMesh = enity->getComponent<StaticMeshComponent>())
    //         if(auto material = staticMesh->getMaterial())
    //             materialDst = material->getDescriptorSet(m_currentFrame);

    //     vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout->vk(), 1, 1, &materialDst, 0, nullptr);

    //     vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout->vk(), 0, 1, &m_descriptorSet[m_currentFrame], 0, nullptr);

    //     vkCmdDrawIndexed(commandBuffer->vk(), mesh->indicesCount, 1, 0, 0, 0);
    // }
}

void OffscreenRenderGraphPass::update(uint32_t currentFrame, uint32_t currentImageIndex, VkFramebuffer fr)
{
    m_currentFrame = currentFrame;
    m_imageIndex = currentImageIndex;
}

ELIX_NESTED_NAMESPACE_END