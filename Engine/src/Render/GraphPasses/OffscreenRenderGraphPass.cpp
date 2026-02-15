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

    // const std::array<std::string, 6> cubemaps{
    //     "./resources/textures/right.jpg",
    //     "./resources/textures/left.jpg",
    //     "./resources/textures/top.jpg",
    //     "./resources/textures/bottom.jpg",
    //     "./resources/textures/front.jpg",
    //     "./resources/textures/back.jpg",
    // };

    // m_skybox = std::make_unique<Skybox>(cubemaps, m_descriptorPool);

    m_skybox = std::make_unique<Skybox>("./resources/textures/default_sky.hdr", m_descriptorPool);
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
    renderPassExecution.renderArea.offset = {0, 0};
    renderPassExecution.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color0{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color0.imageView = m_colorRenderTargets[renderContext.currentImageIndex]->vkImageView();
    color0.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color0.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color0.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color0.clearValue = m_clearValues[0];

    VkRenderingAttachmentInfo color1{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color1.imageView = m_objectIdRenderTarget->vkImageView();
    color1.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color1.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color1.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color1.clearValue = m_clearValues[1];

    VkRenderingAttachmentInfo depthAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAtt.imageView = m_depthRenderTarget->vkImageView();
    depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.clearValue = m_clearValues[2];

    renderPassExecution.colorsRenderingItems = {color0, color1};
    renderPassExecution.depthRenderingItem = depthAtt;

    renderPassExecution.colorFormats = m_colorFormats;
    renderPassExecution.depthFormat = m_depthFormat;

    return {renderPassExecution};
}

void OffscreenRenderGraphPass::onSwapChainResized(renderGraph::RGPResourcesStorage &storage)
{
    m_depthRenderTarget = storage.getTexture(m_depthTextureHandler);
    m_objectIdRenderTarget = storage.getTexture(m_objectIdTextureHandler);

    m_depthRenderTarget->getImage()->transitionImageLayout(m_depthFormat, VK_IMAGE_LAYOUT_UNDEFINED,
                                                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
        m_colorRenderTargets[imageIndex] = storage.getTexture(m_colorTextureHandler[imageIndex]);
}

void OffscreenRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    m_depthRenderTarget = storage.getTexture(m_depthTextureHandler);
    m_objectIdRenderTarget = storage.getTexture(m_objectIdTextureHandler);

    m_depthRenderTarget->getImage()->transitionImageLayout(m_depthFormat, VK_IMAGE_LAYOUT_UNDEFINED,
                                                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    m_colorRenderTargets.resize(core::VulkanContext::getContext()->getSwapchain()->getImages().size());

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
        m_colorRenderTargets[imageIndex] = storage.getTexture(m_colorTextureHandler[imageIndex]);
}

void OffscreenRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    auto swapChainImageFormat = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();

    m_colorFormats = {swapChainImageFormat, VK_FORMAT_R32_UINT};
    m_depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());

    renderGraph::RGPTextureDescription colorTextureDescription{swapChainImageFormat, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT};
    renderGraph::RGPTextureDescription objectIdTextureDescription{VK_FORMAT_R32_UINT, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC};
    renderGraph::RGPTextureDescription depthTextureDescription{m_depthFormat, renderGraph::RGPTextureUsage::DEPTH_STENCIL};

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

void OffscreenRenderGraphPass::endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassContext &context)
{
    m_colorRenderTargets[context.currentImageIndex]->getImage()->insertImageMemoryBarrier(
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, *commandBuffer);

    m_objectIdRenderTarget->getImage()->insertImageMemoryBarrier(
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, *commandBuffer.get());

    m_depthRenderTarget->getImage()->insertImageMemoryBarrier(
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}, *commandBuffer.get());
}

void OffscreenRenderGraphPass::startBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassContext &context)
{
    m_colorRenderTargets[context.currentImageIndex]->getImage()->insertImageMemoryBarrier(
        VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, *commandBuffer);

    m_objectIdRenderTarget->getImage()->insertImageMemoryBarrier(
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, *commandBuffer);

    m_depthRenderTarget->getImage()->insertImageMemoryBarrier(
        VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}, *commandBuffer);
}

void OffscreenRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                                      const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    for (const auto &[entity, drawItem] : data.drawItems)
    {
        auto entityId = entity->getId();

        for (const auto &mesh : drawItem.meshes)
        {
            GraphicsPipelineKey key{};
            key.shader = drawItem.finalBones.empty() ? ShaderId::StaticMesh : ShaderId::SkinnedMesh;
            key.cull = CullMode::Back;
            key.depthTest = true;
            key.depthWrite = true;
            key.depthCompare = VK_COMPARE_OP_LESS;
            key.polygonMode = VK_POLYGON_MODE_FILL;
            key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            key.colorFormats = m_colorFormats;
            key.depthFormat = m_depthFormat;

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

    auto key = m_skybox->getGraphicsPipelineKey();
    key.colorFormats = m_colorFormats;
    key.depthFormat = m_depthFormat;

    auto skyboxRenderGraphPipeline = GraphicsPipelineManager::getOrCreate(key);

    m_skybox->render(commandBuffer, data.view, data.projection, skyboxRenderGraphPipeline);

    for (auto &addData : data.additionalData)
    {
        for (const auto &drawItem : addData.drawItems)
        {
            for (const auto &m : drawItem.meshes)
            {
                auto key = drawItem.graphicsPipelineKey;
                key.colorFormats = m_colorFormats;
                key.depthFormat = m_depthFormat;

                auto graphicsPipeline = GraphicsPipelineManager::getOrCreate(key);
                core::PipelineLayout::SharedPtr pipelineLayout = EngineShaderFamilies::meshShaderFamily.pipelineLayout;

                vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

                VkBuffer vertexBuffers[] = {m->vertexBuffer->vk()};
                VkDeviceSize offset[] = {0};

                vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);
                vkCmdBindIndexBuffer(commandBuffer->vk(), m->indexBuffer->vk(), 0, m->indexType);

                ModelPushConstant modelPushConstant{
                    .model = drawItem.transform};

                vkCmdPushConstants(commandBuffer->vk(), pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ModelPushConstant), &modelPushConstant);

                std::vector<VkDescriptorSet> descriptorSets;

                descriptorSets =
                    {
                        data.cameraDescriptorSet // set 0: camera
                    };

                vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout->vk(), 0, static_cast<uint32_t>(descriptorSets.size()),
                                        descriptorSets.data(), 0, nullptr);

                vkCmdDrawIndexed(commandBuffer->vk(), m->indicesCount, 1, 0, 0, 0);
            }
        }
    }
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END