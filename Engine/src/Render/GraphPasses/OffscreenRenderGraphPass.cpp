#include "Engine/Render/GraphPasses/OffscreenRenderGraphPass.hpp"

#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Builders/RenderPassBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"

#include "Core/VulkanHelpers.hpp"
#include <iostream>

struct ModelPushConstant
{
    glm::mat4 model{1.0f};
    uint32_t objectId{0};
    uint32_t padding[3];
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

OffscreenRenderGraphPass::OffscreenRenderGraphPass(VkDescriptorPool descriptorPool, RGPResourceHandler &shadowTextureHandler) : m_descriptorPool(descriptorPool),
                                                                                                                                m_shadowTextureHandler(shadowTextureHandler)
{
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    m_clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[2].depthStencil = {1.0f, 0};

    this->setDebugName("Offscreen render graph pass");

    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void OffscreenRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = VkViewport{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    m_scissor = VkRect2D{VkOffset2D{0, 0}, m_extent};
}

std::vector<IRenderGraphPass::RenderPassExecution> OffscreenRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution renderPassExecution;
    renderPassExecution.clearValues = {m_clearValues[0], m_clearValues[1], m_clearValues[2]};
    renderPassExecution.framebuffer = m_framebuffers[renderContext.currentImageIndex];
    renderPassExecution.renderArea.offset = {0, 0};
    renderPassExecution.renderArea.extent = m_extent;
    renderPassExecution.renderPass = m_renderPass;
    return {renderPassExecution};
}

void OffscreenRenderGraphPass::onSwapChainResized(renderGraph::RGPResourcesStorage &storage)
{
    auto depthTexture = storage.getTexture(m_depthTextureHandler);
    auto objectIdTexture = storage.getTexture(m_objectIdTextureHandler);

    depthTexture->getImage()->transitionImageLayout(core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice()), VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        auto frameBuffer = m_framebuffers[imageIndex];

        auto colorTexture = storage.getTexture(m_colorTextureHandler[imageIndex]);
        m_colorImages[imageIndex] = colorTexture;

        std::vector<VkImageView> attachments{colorTexture->vkImageView(), objectIdTexture->vkImageView(), depthTexture->vkImageView()};

        frameBuffer->resize(core::VulkanContext::getContext()->getSwapchain()->getExtent(), attachments);
    }
}

void OffscreenRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    auto depthTexture = storage.getTexture(m_depthTextureHandler);
    auto objectIdTexture = storage.getTexture(m_objectIdTextureHandler);

    depthTexture->getImage()->transitionImageLayout(core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice()), VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        auto colorTexture = storage.getTexture(m_colorTextureHandler[imageIndex]);
        m_colorImages.push_back(colorTexture);
        std::vector<VkImageView> attachments{colorTexture->vkImageView(), objectIdTexture->vkImageView(), depthTexture->vkImageView()};

        auto framebuffer = core::Framebuffer::createShared(core::VulkanContext::getContext()->getDevice(), attachments,
                                                           m_renderPass, core::VulkanContext::getContext()->getSwapchain()->getExtent());

        m_framebuffers.push_back(framebuffer);
    }

    // std::array<std::string, 6> cubemaps{
    //     "./resources/textures/right.jpg",
    //     "./resources/textures/left.jpg",
    //     "./resources/textures/top.jpg",
    //     "./resources/textures/bottom.jpg",
    //     "./resources/textures/front.jpg",
    //     "./resources/textures/back.jpg",
    // };

    // m_skybox = std::make_unique<Skybox>(m_device, core::VulkanContext::getContext()->getPhysicalDevice(), core::VulkanContext::getContext()->getTransferCommandPool(), m_renderPass,
    //                                     cubemaps, m_descriptorPool);

    // m_skybox = std::make_unique<Skybox>(m_renderPass, "./resources/textures/default_sky.hdr", m_descriptorPool);
}

void OffscreenRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    auto physicalDevice = core::VulkanContext::getContext()->getPhysicalDevice();

    auto swapChainImageFormat = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();
    auto depthImageFormat = core::helpers::findDepthFormat(physicalDevice);

    m_renderPass = builders::RenderPassBuilder::begin()
                       .addColorAttachment(swapChainImageFormat, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                           VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                       .addColorAttachment(VK_FORMAT_R32_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                           VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                       .addDepthAttachment(depthImageFormat, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                           VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
                                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
                       .addSubpassDependency()
                       .build();

    renderGraph::RGPTextureDescription colorTextureDescription{swapChainImageFormat, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT};
    renderGraph::RGPTextureDescription depthTextureDescription{depthImageFormat, renderGraph::RGPTextureUsage::DEPTH_STENCIL};
    renderGraph::RGPTextureDescription objectIdTextureDescription{VK_FORMAT_R32_UINT, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC};

    colorTextureDescription.setExtent(m_extent);

    objectIdTextureDescription.setDebugName("__ELIX_OBJECT_ID__TEXTURE__");
    objectIdTextureDescription.setExtent(m_extent);

    depthTextureDescription.setDebugName("__ELIX_DEPTH_OFFSCREEN__TEXTURE__");
    depthTextureDescription.setExtent(m_extent);

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        colorTextureDescription.setDebugName("__ELIX_COLOR_OFFSCREEN__TEXTURE_" + std::to_string(imageIndex) + "__");
        auto colorTexture = builder.createTexture(colorTextureDescription);
        m_colorTextureHandler.push_back(colorTexture);
        builder.write(colorTexture, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
    }

    m_objectIdTextureHandler = builder.createTexture(objectIdTextureDescription);

    m_depthTextureHandler = builder.createTexture(depthTextureDescription);
    builder.write(m_depthTextureHandler, renderGraph::RGPTextureUsage::DEPTH_STENCIL);

    builder.read(m_shadowTextureHandler, RGPTextureUsage::SAMPLED);
}

void OffscreenRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                                      const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    for (const auto &[entity, drawItem] : data.drawItems)
    {
        uint64_t entityId = entity->getId();

        for (const auto &mesh : drawItem.meshes)
        {
            GraphicsPipelineKey key{};
            key.renderPass = m_renderPass;
            key.shader = drawItem.finalBones.empty() ? ShaderId::StaticMesh : ShaderId::SkinnedMesh;
            key.cull = CullMode::Back;
            key.depthTest = true;
            key.depthWrite = true;
            key.depthCompare = VK_COMPARE_OP_LESS;
            key.polygonMode = VK_POLYGON_MODE_FILL;
            key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            auto graphicsPipeline = GraphicsPipelineManager::getOrCreate(key);
            auto pipelineLayout = EngineShaderFamilies::meshShaderFamily.pipelineLayout;

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

            VkBuffer vertexBuffers[] = {mesh->vertexBuffer};
            VkDeviceSize offset[] = {0};

            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offset);
            vkCmdBindIndexBuffer(commandBuffer, mesh->indexBuffer, 0, mesh->indexType);

            ModelPushConstant modelPushConstant{
                .model = drawItem.transform, .objectId = entityId};

            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ModelPushConstant), &modelPushConstant);

            std::vector<VkDescriptorSet> descriptorSets{
                data.cameraDescriptorSet,                                     // set 0: camera & light
                mesh->material->getDescriptorSet(renderContext.currentFrame), // set 1: material
                data.perObjectDescriptorSet                                   // set 2 : per object(Only bones for now)
            };

            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, static_cast<uint32_t>(descriptorSets.size()),
                                    descriptorSets.data(), 0, nullptr);

            vkCmdDrawIndexed(commandBuffer, mesh->indicesCount, 1, 0, 0, 0);
        }
    }

    // m_skybox->render(commandBuffer, data.view, data.projection);

    // for (auto &addData : data.additionalData)
    // {
    //     for (const auto &drawItem : addData.drawItems)
    //     {
    //         for (const auto &m : drawItem.meshes)
    //         {
    //             auto key = drawItem.graphicsPipelineKey;
    //             key.renderPass = m_renderPass;

    //             auto graphicsPipeline = GraphicsPipelineManager::getOrCreate(key);
    //             core::PipelineLayout::SharedPtr pipelineLayout = EngineShaderFamilies::meshShaderFamily.pipelineLayout;

    //             vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    //             VkBuffer vertexBuffers[] = {m->vertexBuffer->vk()};
    //             VkDeviceSize offset[] = {0};

    //             vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);
    //             vkCmdBindIndexBuffer(commandBuffer->vk(), m->indexBuffer->vk(), 0, m->indexType);

    //             ModelPushConstant modelPushConstant{
    //                 .model = drawItem.transform};

    //             vkCmdPushConstants(commandBuffer->vk(), pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ModelPushConstant), &modelPushConstant);

    //             std::vector<VkDescriptorSet> descriptorSets;

    //             descriptorSets =
    //                 {
    //                     data.cameraDescriptorSet // set 0: camera
    //                 };

    //             vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout->vk(), 0, static_cast<uint32_t>(descriptorSets.size()),
    //                                     descriptorSets.data(), 0, nullptr);

    //             vkCmdDrawIndexed(commandBuffer->vk(), m->indicesCount, 1, 0, 0, 0);
    //         }
    //     }
    // }
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END