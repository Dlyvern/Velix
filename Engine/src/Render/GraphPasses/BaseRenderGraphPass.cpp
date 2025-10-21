#include "Engine/Render/GraphPasses/BaseRenderGraphPass.hpp"

#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/GraphicsPipelineBuilder.hpp"
#include "Engine/ShaderDataExtractor.hpp"

#include "Core/Shader.hpp"
#include "Core/VulkanHelpers.hpp"

#include <iostream>

struct ModelPushConstant
{
    glm::mat4 model{1.0f};
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)

BaseRenderGraphPass::BaseRenderGraphPass(VkDevice device, core::SwapChain::SharedPtr swapchain, 
core::PipelineLayout::SharedPtr pipelineLayout) 
: m_device(device), m_swapchain(swapchain), m_pipelineLayout(pipelineLayout)
{
    auto queueFamilyIndices = core::VulkanContext::findQueueFamilies(core::VulkanContext::getContext()->getPhysicalDevice(), core::VulkanContext::getContext()->getSurface());
    m_commandPool = core::CommandPool::create(device, queueFamilyIndices.graphicsFamily.value());
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    m_clearValues[1].depthStencil = {1.0f, 0};
}

void BaseRenderGraphPass::getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const
{
    renderPassBeginInfo = VkRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass = m_renderPass->vk();
    renderPassBeginInfo.framebuffer = m_framebuffers[m_imageIndex]->vk();
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = m_swapchain->getExtent();
    renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(m_clearValues.size());
    renderPassBeginInfo.pClearValues = m_clearValues.data();
}

void BaseRenderGraphPass::setup(RenderGraphPassRecourceBuilder& graphPassBuilder)
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchain->getImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentReference{};
    colorAttachmentReference.attachment = 0;
    colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentReference{};
    depthAttachmentReference.attachment = 1;
    depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentReference;
    subpass.pDepthStencilAttachment = &depthAttachmentReference;

    std::vector<VkSubpassDependency> dependency;
    dependency.resize(1);
    dependency[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency[0].dstSubpass = 0;
    dependency[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency[0].srcAccessMask = 0;
    dependency[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::vector<VkAttachmentDescription> attachments{colorAttachment, depthAttachment};

    m_renderPass = core::RenderPass::create(m_device, attachments, {subpass}, dependency);

    RenderGraphPassResourceTypes::RenderPassDescription renderPassDescription
    {
        .name = "__ELIX_SWAP_CHAIN_RENDER_PASS__",
        .attachments = attachments,
        .subpassDescriptions = {},
        .subpassDependencies = dependency,
        .colorAttachments = {{colorAttachmentReference}},
        .depthAttachments = {{depthAttachmentReference}},
    };

    m_renderPassHash = graphPassBuilder.createRenderPass(renderPassDescription);

    RenderGraphPassResourceTypes::SizeSpec sizeSpec
    {
        .type = RenderGraphPassResourceTypes::SizeClass::SwapchainRelative,
    };

    RenderGraphPassResourceTypes::TextureDescription textureDescription{
        .name = "__ELIX_SWAP_CHAIN_DEPTH__",
        .format = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice()),
        .size = sizeSpec,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
        .properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
    };
    
    RenderGraphPassResourceTypes::TextureDescription colorTexture{
        .name = "__ELIX_SWAP_CHAIN_COLOR__",
        .size = sizeSpec,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
        .source = RenderGraphPassResourceTypes::TextureDescription::FormatSource::Swapchain
    };

    m_depthImageHash = graphPassBuilder.createTexture(textureDescription, {.access = ResourceAccess::WRITE, .user = this});
    auto colorImageBaseHash = graphPassBuilder.createTexture(colorTexture, {.access = ResourceAccess::WRITE, .user = this});

    for(size_t i = 0; i < m_swapchain->getImages().size(); ++i)
    {
        const std::string name = "__ELIX_SWAP_CHAIN_FRAMEBUFFER_" + std::to_string(i) + "__";

        RenderGraphPassResourceTypes::FramebufferDescription framebufferDescription{
            .name = name,
            .attachmentsHash = {colorImageBaseHash + i, m_depthImageHash},
            .renderPass = m_renderPass,
            .size = sizeSpec,
            .layers = 1
        };

        m_framebufferHashes.push_back(graphPassBuilder.createFramebuffer(framebufferDescription));
    }
}

void BaseRenderGraphPass::compile(RenderGraphPassResourceHash& storage)
{
    auto depthTexture = storage.getTexture(m_depthImageHash);
    // m_renderPass = storage.getRenderPass(m_renderPassHash);

    //TODO add transition on swapChain recreation
    if(depthTexture)
        depthTexture->getImage()->transitionImageLayout(core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice()), VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_commandPool, core::VulkanContext::getContext()->getGraphicsQueue());
    else
        std::cerr << "Failed to find a depth texture for BaseRenderGraphPass" << std::endl;
    
    for(const auto& framebufferCache : m_framebufferHashes)
    {
        auto framebuffer = storage.getFramebuffer(framebufferCache);

        if(!framebuffer)
        {
            std::cerr << "Failed to find a framebuffer for BaseRenderGraphPass" << std::endl;
            continue;
        }

        m_framebuffers.push_back(framebuffer);
    }

    core::Shader shader("./resources/shaders/static_mesh.vert.spv", "./resources/shaders/static_mesh.frag.spv");

    const auto& vertexHandler = shader.getVertexHandler();

    ShaderDataExtractor::parse(vertexHandler, "./resources/shaders/static_mesh.vert.spv");

    auto bindingDescription = engine::Vertex3D::getBindingDescription();
    auto attributeDescription = engine::Vertex3D::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescription.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescription.data();

    auto viewport = m_swapchain->getViewport();
    auto scissor = m_swapchain->getScissor();

    engine::GraphicsPipelineBuilder graphicsPipelineBuilder;
    graphicsPipelineBuilder.layout = m_pipelineLayout->vk();
    graphicsPipelineBuilder.renderPass = m_renderPass->vk();
    graphicsPipelineBuilder.viewportState.pViewports = &viewport;
    graphicsPipelineBuilder.viewportState.pScissors = &scissor;
    graphicsPipelineBuilder.shaderStages = shader.getShaderStages();

    m_graphicsPipeline = graphicsPipelineBuilder.build(m_device, vertexInputInfo);
}

void BaseRenderGraphPass::update(const RenderGraphPassContext& renderData)
{
    m_imageIndex = renderData.currentImageIndex;
    m_currentFrame = renderData.currentFrame;
}

void BaseRenderGraphPass::execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData& data)
{
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline->vk());

    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &data.swapChainViewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &data.swapChainScissor);

    for(const auto [entity, gpuEntity] : data.meshes)
    {
        VkBuffer vertexBuffers[] = {gpuEntity.mesh->vertexBuffer->vkBuffer()};
        VkDeviceSize offset[] = {0};

        vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);
        vkCmdBindIndexBuffer(commandBuffer->vk(), gpuEntity.mesh->indexBuffer->vkBuffer(), 0, gpuEntity.mesh->indexType);

        ModelPushConstant modelPushConstant
        {
            .model = gpuEntity.transform
        };

        vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ModelPushConstant), &modelPushConstant);

        const std::array<VkDescriptorSet, 3> descriptorSets = 
        {
            data.cameraDescriptorSet, // set 0: camera
            gpuEntity.materialDescriptorSet,  // set 1: material
            data.lightDescriptorSet // set 2: lighting
        };

        vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout->vk(), 0, static_cast<uint32_t>(descriptorSets.size()), 
        descriptorSets.data(), 0, nullptr);

        vkCmdDrawIndexed(commandBuffer->vk(), gpuEntity.mesh->indicesCount, 1, 0, 0, 0);
    }
}

ELIX_NESTED_NAMESPACE_END