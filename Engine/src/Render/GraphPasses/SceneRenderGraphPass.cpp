#include "Engine/Render/GraphPasses/SceneRenderGraphPass.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Core/VulkanHelpers.hpp"

#include "Engine/Builders/GraphicsPipelineManager.hpp"

#include <iostream>

struct ModelPushConstant
{
    glm::mat4 model{1.0f};
    uint32_t objectId{0};
    uint32_t padding[3];
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

SceneRenderGraphPass::SceneRenderGraphPass(renderGraph::RGPResourceHandler &shadowHandler) : m_shadowHandler(shadowHandler)
{
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    m_clearValues[1].color = {0.0f, 0.0f, 0.0f, 0.0f};
    m_clearValues[2].depthStencil = {1.0f, 0};
    this->setDebugName("Screne render graph pass");
}

void SceneRenderGraphPass::setup(RGPResourcesBuilder &builder)
{
    m_colorFormats = {core::VulkanContext::getContext()->getSwapchain()->getImageFormat(), VK_FORMAT_R32_UINT};
    m_depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());

    RGPTextureDescription colorTextureDescription{core::VulkanContext::getContext()->getSwapchain()->getImageFormat(), RGPTextureUsage::COLOR_ATTACHMENT};
    RGPTextureDescription objectIdTextureDescription{VK_FORMAT_R32_UINT, RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC};

    RGPTextureDescription depthTextureDescription{m_depthFormat, RGPTextureUsage::DEPTH_STENCIL};

    colorTextureDescription.setDebugName("__ELIX_COLOR_TEXTURE__");
    colorTextureDescription.setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    colorTextureDescription.setIsSwapChainTarget(true);

    objectIdTextureDescription.setDebugName("__ELIX_OBJECT_ID_TEXTURE__");
    objectIdTextureDescription.setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());

    depthTextureDescription.setDebugName("__ELIX_DEPTH_TEXTURE__");
    depthTextureDescription.setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());

    m_colorTextureHandler = builder.createTexture(colorTextureDescription);
    m_depthTextureHandler = builder.createTexture(depthTextureDescription);
    m_objectIdTextureHandler = builder.createTexture(objectIdTextureDescription);

    builder.write(m_colorTextureHandler, RGPTextureUsage::COLOR_ATTACHMENT);
    builder.write(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
    builder.write(m_objectIdTextureHandler, RGPTextureUsage::COLOR_ATTACHMENT);

    builder.read(m_shadowHandler, RGPTextureUsage::SAMPLED);
}

void SceneRenderGraphPass::compile(RGPResourcesStorage &storage)
{
    m_depthRenderTarget = storage.getTexture(m_depthTextureHandler);
    m_objectIdRenderTarget = storage.getTexture(m_objectIdTextureHandler);

    m_depthRenderTarget->getImage()->transitionImageLayout(m_depthFormat, VK_IMAGE_LAYOUT_UNDEFINED,
                                                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    m_colorRenderTargets.resize(core::VulkanContext::getContext()->getSwapchain()->getImages().size());

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
        m_colorRenderTargets[imageIndex] = storage.getSwapChainTexture(m_colorTextureHandler, imageIndex);
}

void SceneRenderGraphPass::onSwapChainResized(renderGraph::RGPResourcesStorage &storage)
{
    m_depthRenderTarget = storage.getTexture(m_depthTextureHandler);
    m_objectIdRenderTarget = storage.getTexture(m_objectIdTextureHandler);

    m_depthRenderTarget->getImage()->transitionImageLayout(m_depthFormat, VK_IMAGE_LAYOUT_UNDEFINED,
                                                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
        m_colorRenderTargets[imageIndex] = storage.getSwapChainTexture(m_colorTextureHandler, imageIndex);
}

std::vector<IRenderGraphPass::RenderPassExecution> SceneRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution renderPassExecution;
    renderPassExecution.renderArea.offset = {0, 0};
    renderPassExecution.renderArea.extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();

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

void SceneRenderGraphPass::startBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassContext &context)
{
    m_colorRenderTargets[context.currentImageIndex]->getImage()->insertImageMemoryBarrier(
        0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
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
        0,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}, *commandBuffer);
}

void SceneRenderGraphPass::endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassContext &context)
{
    m_colorRenderTargets[context.currentImageIndex]->getImage()->insertImageMemoryBarrier(
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        0,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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

void SceneRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                                  const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer, 0, 1, &data.swapChainViewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &data.swapChainScissor);

    auto &pipelineLayout = EngineShaderFamilies::meshShaderFamily.pipelineLayout;

    for (const auto &[entity, drawItem] : data.drawItems)
    {
        auto entityId = entity->getId();

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

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        for (const auto &mesh : drawItem.meshes)
        {
            VkBuffer vertexBuffers[] = {mesh->vertexBuffer};
            VkDeviceSize offset[] = {0};

            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offset);
            vkCmdBindIndexBuffer(commandBuffer, mesh->indexBuffer, 0, mesh->indexType);

            ModelPushConstant modelPushConstant{
                .model = drawItem.transform, .objectId = entityId};

            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(ModelPushConstant), &modelPushConstant);

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
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END