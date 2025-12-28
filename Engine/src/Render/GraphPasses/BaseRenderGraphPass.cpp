#include "Engine/Render/GraphPasses/BaseRenderGraphPass.hpp"

#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/ShaderDataExtractor.hpp"

#include "Engine/ShaderFamily.hpp"

#include "Core/Shader.hpp"
#include "Core/VulkanHelpers.hpp"

#include <iostream>

struct ModelPushConstant
{
    glm::mat4 model{1.0f};
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)

BaseRenderGraphPass::BaseRenderGraphPass(VkDevice device, core::SwapChain::SharedPtr swapchain, VkDescriptorPool descriptorPool, SceneRenderMask usageMask) 
: m_device(device), m_swapchain(swapchain), m_descriptorPool(descriptorPool), m_usageMask(usageMask)
{
    m_commandPool = core::CommandPool::createShared(device, core::VulkanContext::getContext()->getGraphicsFamily());
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    m_clearValues[1].depthStencil = {1.0f, 0};
    m_pipelineLayout = engineShaderFamilies::staticMeshShaderFamily.pipelineLayout;
}

void BaseRenderGraphPass::getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const
{
    renderPassBeginInfo = VkRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    // renderPassBeginInfo.renderPass = m_offscreenRenderPass->vk();
    // renderPassBeginInfo.framebuffer = m_offscreenFramebuffers[m_imageIndex]->vk();
    renderPassBeginInfo.renderPass = m_renderPass->vk();
    renderPassBeginInfo.framebuffer = m_framebuffers[m_imageIndex]->vk();
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = m_swapchain->getExtent();
    renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(m_clearValues.size());
    renderPassBeginInfo.pClearValues = m_clearValues.data();
}

void BaseRenderGraphPass::setupOffscreenResources(RenderGraphPassRecourceBuilder& graphPassBuilder)
{

}

void BaseRenderGraphPass::setupSwapChainResources(RenderGraphPassRecourceBuilder& graphPassBuilder)
{
    
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

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    RenderGraphPassResourceTypes::RenderPassDescription::SubpassDescription subpassDescription
    {
        .colorAttachmentCount = 1,
        .colorAttachments = {colorAttachmentReference},
        .depthStencilAttachments = {depthAttachmentReference}
    };

    RenderGraphPassResourceTypes::RenderPassDescription renderPassDescription
    {
        .name = "__ELIX_SWAP_CHAIN_RENDER_PASS__",
        .attachments = {colorAttachment, depthAttachment},
        .subpassDescriptions = {subpassDescription},
        .subpassDependencies = {dependency},
    };

    m_renderPassHash = graphPassBuilder.createRenderPass(renderPassDescription);

    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    renderPassDescription.name = "__ELIX_OFFSCREEN_RENDER_PASS__";
    renderPassDescription.attachments = {colorAttachment, depthAttachment};

    m_offscreenRenderPassHash = graphPassBuilder.createRenderPass(renderPassDescription);

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
        .memoryFlags = core::memory::MemoryUsage::GPU_ONLY,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
    };
    
    RenderGraphPassResourceTypes::TextureDescription colorTexture{
        .name = "__ELIX_SWAP_CHAIN_COLOR__",
        .size = sizeSpec,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
        .source = RenderGraphPassResourceTypes::TextureDescription::FormatSource::Swapchain
    };

    RenderGraphPassResourceTypes::TextureDescription offscreenColorTexture{
        .format = m_swapchain->getImageFormat(),
        .size = sizeSpec,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
        .memoryFlags = core::memory::MemoryUsage::CPU_TO_GPU,
        // .properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
    };

    m_depthImageHash = graphPassBuilder.createTexture(textureDescription, {.access = ResourceAccess::WRITE, .user = this});
    auto colorImageBaseHash = graphPassBuilder.createTexture(colorTexture, {.access = ResourceAccess::WRITE, .user = this});

    for(size_t i = 0; i < m_swapchain->getImages().size(); ++i)
    {
        const std::string name = "__ELIX_SWAP_CHAIN_FRAMEBUFFER_" + std::to_string(i) + "__";

        RenderGraphPassResourceTypes::FramebufferDescription framebufferDescription{
            .name = name,
            .attachmentsHash = {colorImageBaseHash + i, m_depthImageHash},
            .renderPassHash = m_renderPassHash,
            .size = sizeSpec,
            .layers = 1
        };

        m_framebufferHashes.push_back(graphPassBuilder.createFramebuffer(framebufferDescription));
    }

    for(int i = 0; i < m_swapchain->getImages().size(); ++i)
    {
        const std::string name = "__ELIX_OFFSCREEN_COLOR_" + std::to_string(i) + "__";
        offscreenColorTexture.name = name;
        m_colorTextureHashes.push_back(graphPassBuilder.createTexture(offscreenColorTexture, {.access = ResourceAccess::WRITE, .user = this}));
    }

    for(size_t i = 0; i < m_colorTextureHashes.size(); ++i)
    {
        const std::string name = "__ELIX_OFFSCREEN_FRAMEBUFFER_" + std::to_string(i) + "__";

        RenderGraphPassResourceTypes::FramebufferDescription framebufferDescription{
            .name = name,
            .attachmentsHash = {m_colorTextureHashes[i], m_depthImageHash},
            .renderPassHash = m_offscreenRenderPassHash,
            .size = sizeSpec,
            .layers = 1
        };

        m_offscreenFramebufferHashes.push_back(graphPassBuilder.createFramebuffer(framebufferDescription));
    }

    auto shader = std::make_shared<core::Shader>("./resources/shaders/static_mesh.vert.spv", "./resources/shaders/static_mesh.frag.spv");

    RenderGraphPassResourceTypes::GraphicsPipelineDescription graphicsPipeline
    {
        .name = "__ELIX_SWAP_CHAIN_GRAPHICS_PIPELINE__",
        .vertexBindingDescriptions = {engine::Vertex3D::getBindingDescription()},
        .vertexAttributeDescriptions = engine::Vertex3D::getAttributeDescriptions(),
        .layout = m_pipelineLayout->vk(),
        .renderPassHash = m_renderPassHash
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment
    {
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    graphicsPipeline.colorBlendingAttachments.push_back(colorBlendAttachment);

    graphicsPipeline.colorBlending.blendConstants[0] = 0.0f;
    graphicsPipeline.colorBlending.blendConstants[1] = 0.0f;
    graphicsPipeline.colorBlending.blendConstants[2] = 0.0f;
    graphicsPipeline.colorBlending.blendConstants[3] = 0.0f;
    graphicsPipeline.dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    graphicsPipeline.viewport = m_swapchain->getViewport();
    graphicsPipeline.scissor= m_swapchain->getScissor();
    graphicsPipeline.shader = shader;

    m_graphicsPipelineHash = graphPassBuilder.createGraphicsPipeline(graphicsPipeline);

    graphicsPipeline.renderPassHash = m_offscreenRenderPassHash;

    m_offscreenGraphicsPipelineHash = graphPassBuilder.createGraphicsPipeline(graphicsPipeline);
}

void BaseRenderGraphPass::compile(RenderGraphPassResourceHash& storage)
{
    m_graphicsPipeline = storage.getGraphicsPipeline(m_graphicsPipelineHash);
    m_renderPass = storage.getRenderPass(m_renderPassHash);

    m_offscreenGraphicsPipeline = storage.getGraphicsPipeline(m_offscreenGraphicsPipelineHash);
    m_offscreenRenderPass = storage.getRenderPass(m_offscreenRenderPassHash);

    if(!m_graphicsPipeline)
        std::cerr << "Failed to find a graphics pipeline for BaseRenderGraphPass" << std::endl;
    if(!m_renderPass)
        std::cerr << "Failed to find a render pass for BaseRenderGraphPass" << std::endl;

    auto depthTexture = storage.getTexture(m_depthImageHash);

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

    for(const auto& framebufferCache : m_offscreenFramebufferHashes)
    {
        auto framebuffer = storage.getFramebuffer(framebufferCache);

        if(!framebuffer)
        {
            std::cerr << "Failed to find a framebuffer for BaseRenderGraphPass" << std::endl;
            continue;
        }

        m_offscreenFramebuffers.push_back(framebuffer);
    }

    for(const auto& colorHash : m_colorTextureHashes)
    {
        auto colorTexture = storage.getTexture(colorHash);

        if(!colorTexture)
        {
            std::cerr << "Failed to find a color texture for OffscreenRenderGraphPass" << std::endl;
            continue;
        }

        colorTexture->getImage()->insertImageMemoryBarrier(VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VkImageSubresourceRange{
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, m_commandPool, core::VulkanContext::getContext()->getGraphicsQueue());

        m_offscreenColorImages.push_back(colorTexture);
    }   

    std::array<std::string, 6> cubemaps
    {
        "./resources/textures/right.jpg",
        "./resources/textures/left.jpg",
        "./resources/textures/top.jpg",
        "./resources/textures/bottom.jpg",
        "./resources/textures/front.jpg",
        "./resources/textures/back.jpg",
    };

    m_skybox = std::make_unique<Skybox>(m_device, core::VulkanContext::getContext()->getPhysicalDevice(), m_commandPool, m_renderPass,
    cubemaps, m_descriptorPool);

    m_offscreenSkybox = std::make_unique<Skybox>(m_device, core::VulkanContext::getContext()->getPhysicalDevice(), m_commandPool, 
    m_offscreenRenderPass, cubemaps, m_descriptorPool);
}

void BaseRenderGraphPass::update(const RenderGraphPassContext& renderData)
{
    m_imageIndex = renderData.currentImageIndex;
    m_currentFrame = renderData.currentFrame;
}

void BaseRenderGraphPass::execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData& data)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &data.swapChainViewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &data.swapChainScissor);

    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline->vk());
    // vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_offscreenGraphicsPipeline->vk());

    for(const auto& [entity, gpuEntity] : data.meshes)
    {
        for(const auto& mesh : gpuEntity.meshes)
        {
            VkBuffer vertexBuffers[] = {mesh->vertexBuffer->vk()};
            VkDeviceSize offset[] = {0};

            vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);
            vkCmdBindIndexBuffer(commandBuffer->vk(), mesh->indexBuffer->vk(), 0, mesh->indexType);

            ModelPushConstant modelPushConstant
            {
                .model = gpuEntity.transform
            };

            vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ModelPushConstant), &modelPushConstant);

            const std::array<VkDescriptorSet, 3> descriptorSets = 
            {
                data.cameraDescriptorSet, // set 0: camera
                mesh->material->getDescriptorSet(m_currentFrame),  // set 1: material
                data.lightDescriptorSet // set 2: lighting
            };

            vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout->vk(), 0, static_cast<uint32_t>(descriptorSets.size()), 
            descriptorSets.data(), 0, nullptr);

            vkCmdDrawIndexed(commandBuffer->vk(), mesh->indicesCount, 1, 0, 0, 0);
        }
    }
    
    m_skybox->render(commandBuffer, data.view, data.projection);
    // m_offscreenSkybox->render(commandBuffer, data.view, data.projection);
}

ELIX_NESTED_NAMESPACE_END