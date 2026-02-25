#include "Engine/Render/GraphPasses/GBufferRenderGraphPass.hpp"

#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Render/ObjectIdEncoding.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

struct ModelPushConstant
{
    glm::mat4 model{1.0f};
    uint32_t objectId{0};
    uint32_t bonesOffset{0};
    uint32_t padding[2];
};

GBufferRenderGraphPass::GBufferRenderGraphPass()
{
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    m_clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[3].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[4].depthStencil = {1.0f, 0};

    this->setDebugName("GBuffer render graph pass");

    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void GBufferRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                                    const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    for (const auto &[entity, drawItem] : data.drawItems)
    {
        auto entityId = entity->getId();

        for (uint32_t meshIndex = 0; meshIndex < drawItem.meshes.size(); ++meshIndex)
        {
            const auto &mesh = drawItem.meshes[meshIndex];

            GraphicsPipelineKey key{};
            key.shader = drawItem.finalBones.empty() ? ShaderId::GBufferStatic : ShaderId::GBufferSkinned;
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
                .model = drawItem.transform,
                .objectId = render::encodeObjectId(entityId, meshIndex),
                .bonesOffset = drawItem.bonesOffset};

            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ModelPushConstant), &modelPushConstant);

            std::vector<VkDescriptorSet> descriptorSets{
                data.cameraDescriptorSet,                                     // set 0: camera & light
                mesh->material->getDescriptorSet(renderContext.currentFrame), // set 1: material
                data.perObjectDescriptorSet                                   // set 2 : per object(Only bones for now)
            };

            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, static_cast<uint32_t>(descriptorSets.size()),
                                    descriptorSets.data(), 0, nullptr);

            profiling::cmdDrawIndexed(commandBuffer, mesh->indicesCount, 1, 0, 0, 0);
        }
    }
}

std::vector<IRenderGraphPass::RenderPassExecution> GBufferRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution renderPassExecution;
    renderPassExecution.renderArea.offset = {0, 0};
    renderPassExecution.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo normalColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    normalColor.imageView = m_normalRenderTargets[renderContext.currentImageIndex]->vkImageView();
    normalColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    normalColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    normalColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    normalColor.clearValue = m_clearValues[0];

    VkRenderingAttachmentInfo albedoColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    albedoColor.imageView = m_albedoRenderTargets[renderContext.currentImageIndex]->vkImageView();
    albedoColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    albedoColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    albedoColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    albedoColor.clearValue = m_clearValues[1];

    VkRenderingAttachmentInfo materialColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    materialColor.imageView = m_materialRenderTargets[renderContext.currentImageIndex]->vkImageView();
    materialColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    materialColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    materialColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    materialColor.clearValue = m_clearValues[2];

    VkRenderingAttachmentInfo objectColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    objectColor.imageView = m_objectIdRenderTarget->vkImageView();
    objectColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    objectColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    objectColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    objectColor.clearValue = m_clearValues[3];

    VkRenderingAttachmentInfo depthAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAtt.imageView = m_depthRenderTarget->vkImageView();
    depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAtt.clearValue = m_clearValues[4];

    renderPassExecution.colorsRenderingItems = {normalColor, albedoColor, materialColor, objectColor};
    renderPassExecution.depthRenderingItem = depthAtt;

    renderPassExecution.colorFormats = m_colorFormats;
    renderPassExecution.depthFormat = m_depthFormat;

    renderPassExecution.targets[m_normalTextureHandlers[renderContext.currentImageIndex]] = m_normalRenderTargets[renderContext.currentImageIndex];
    renderPassExecution.targets[m_albedoTextureHandlers[renderContext.currentImageIndex]] = m_albedoRenderTargets[renderContext.currentImageIndex];
    renderPassExecution.targets[m_materialTextureHandlers[renderContext.currentImageIndex]] = m_materialRenderTargets[renderContext.currentImageIndex];

    renderPassExecution.targets[m_objectIdTextureHandler] = m_objectIdRenderTarget;
    renderPassExecution.targets[m_depthTextureHandler] = m_depthRenderTarget;

    return {renderPassExecution};
}

void GBufferRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = VkViewport{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    m_scissor = VkRect2D{VkOffset2D{0, 0}, m_extent};
    requestRecompilation();
}

void GBufferRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    m_depthRenderTarget = storage.getTexture(m_depthTextureHandler);
    m_objectIdRenderTarget = storage.getTexture(m_objectIdTextureHandler);

    int imageCount = core::VulkanContext::getContext()->getSwapchain()->getImages().size();

    m_normalRenderTargets.resize(imageCount);
    m_albedoRenderTargets.resize(imageCount);
    m_materialRenderTargets.resize(imageCount);

    for (int imageIndex = 0; imageIndex < imageCount; ++imageIndex)
    {
        m_normalRenderTargets[imageIndex] = storage.getTexture(m_normalTextureHandlers[imageIndex]);
        m_albedoRenderTargets[imageIndex] = storage.getTexture(m_albedoTextureHandlers[imageIndex]);
        m_materialRenderTargets[imageIndex] = storage.getTexture(m_materialTextureHandlers[imageIndex]);
    }
}

void GBufferRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    auto hdrImageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    m_colorFormats = {hdrImageFormat, hdrImageFormat, hdrImageFormat, VK_FORMAT_R32_UINT};
    m_depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());

    RGPTextureDescription normalTextureDescription{hdrImageFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription albedoTextureDescription{hdrImageFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription materialTextureDescription{hdrImageFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription objectIdTextureDescription{VK_FORMAT_R32_UINT, RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription depthTextureDescription{m_depthFormat, RGPTextureUsage::DEPTH_STENCIL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};

    normalTextureDescription.setCustomExtentFunction([this]
                                                     { return m_extent; });
    albedoTextureDescription.setCustomExtentFunction([this]
                                                     { return m_extent; });
    materialTextureDescription.setCustomExtentFunction([this]
                                                       { return m_extent; });

    objectIdTextureDescription.setDebugName("__ELIX_OBJECT_ID_GBUFFER_TEXTURE__");
    objectIdTextureDescription.setCustomExtentFunction([this]
                                                       { return m_extent; });

    m_objectIdTextureHandler = builder.createTexture(objectIdTextureDescription);
    builder.write(m_objectIdTextureHandler, RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC);

    depthTextureDescription.setDebugName("__ELIX_DEPTH_GBUFFER_TEXTURE__");
    depthTextureDescription.setCustomExtentFunction([this]
                                                    { return m_extent; });

    m_depthTextureHandler = builder.createTexture(depthTextureDescription);
    builder.write(m_depthTextureHandler, renderGraph::RGPTextureUsage::DEPTH_STENCIL);

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        normalTextureDescription.setDebugName("__ELIX_NORMAL_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");
        albedoTextureDescription.setDebugName("__ELIX_ALBEDO_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");
        materialTextureDescription.setDebugName("__ELIX_MATERIAL_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");

        auto normalTexture = builder.createTexture(normalTextureDescription);
        auto albedoTexture = builder.createTexture(albedoTextureDescription);
        auto materialTexture = builder.createTexture(materialTextureDescription);

        m_normalTextureHandlers.push_back(normalTexture);
        m_albedoTextureHandlers.push_back(albedoTexture);
        m_materialTextureHandlers.push_back(materialTexture);

        builder.write(normalTexture, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(albedoTexture, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(materialTexture, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
    }
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
