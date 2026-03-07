#include "Engine/Render/GraphPasses/GBufferRenderGraphPass.hpp"

#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
struct ModelPushConstant
{
    uint32_t baseInstance{0};
    uint32_t padding[3]{0, 0, 0};
};
}

GBufferRenderGraphPass::GBufferRenderGraphPass()
{
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    m_clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[3].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[4].color = {{0.5f, 0.5f, 0.5f, 0.0f}};
    m_clearValues[5].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[6].depthStencil = {1.0f, 0};

    setDebugName("GBuffer render graph pass");
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void GBufferRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                    const RenderGraphPassPerFrameData &data,
                                    const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    const auto drawBatches = [&](const std::vector<DrawBatch> &batches)
    {
        if (batches.empty())
            return;

        const auto pipelineLayout = EngineShaderFamilies::meshShaderFamily.pipelineLayout;

        auto makeKey = [](bool skinned, bool alphaBlend, bool twoSided) -> GraphicsPipelineKey
        {
            GraphicsPipelineKey key{};
            key.shader = skinned ? ShaderId::GBufferSkinned : ShaderId::GBufferStatic;
            key.blend = alphaBlend ? BlendMode::AlphaBlend : BlendMode::None;
            key.cull = twoSided ? CullMode::None : CullMode::Back;
            key.depthTest = true;
            key.depthWrite = !alphaBlend;
            key.depthCompare = VK_COMPARE_OP_LESS;
            key.polygonMode = VK_POLYGON_MODE_FILL;
            key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            return key;
        };

        std::unordered_map<GraphicsPipelineKey, VkPipeline, GraphicsPipelineKeyHash> pipelineCache;
        pipelineCache.reserve(8u);

        auto getPipelineForBatch = [&](const DrawBatch &batch) -> VkPipeline
        {
            const uint32_t materialFlags = batch.material ? batch.material->params().flags : 0u;
            const bool alphaBlend = (materialFlags & Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND) != 0u;
            const bool twoSided = (materialFlags & Material::MaterialFlags::EMATERIAL_FLAG_DOUBLE_SIDED) != 0u;

            GraphicsPipelineKey key = makeKey(batch.skinned, alphaBlend, twoSided);
            key.gbufferOutputMode = GBufferOutputMode::Full;
            key.colorFormats = m_colorFormats;
            key.depthFormat = m_depthFormat;

            const auto pipelineIt = pipelineCache.find(key);
            if (pipelineIt != pipelineCache.end())
                return pipelineIt->second;

            const VkPipeline pipeline = GraphicsPipelineManager::getOrCreate(key);
            pipelineCache.emplace(std::move(key), pipeline);
            return pipeline;
        };

        VkPipeline boundPipeline = VK_NULL_HANDLE;
        VkBuffer boundVertexBuffer = VK_NULL_HANDLE;
        VkBuffer boundIndexBuffer = VK_NULL_HANDLE;
        VkDescriptorSet boundMaterialSet = VK_NULL_HANDLE;

        for (const auto &batch : batches)
        {
            if (!batch.mesh || !batch.material || batch.instanceCount == 0)
                continue;

            const VkPipeline batchPipeline = getPipelineForBatch(batch);
            if (batchPipeline != boundPipeline)
            {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batchPipeline);
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                        0, 1, &data.cameraDescriptorSet, 0, nullptr);
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                        2, 1, &data.perObjectDescriptorSet, 0, nullptr);
                boundPipeline = batchPipeline;
                boundMaterialSet = VK_NULL_HANDLE;
            }

            const VkBuffer vb = batch.mesh->vertexBuffer;
            if (vb != boundVertexBuffer)
            {
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, &offset);
                boundVertexBuffer = vb;
            }

            const VkBuffer ib = batch.mesh->indexBuffer;
            if (ib != boundIndexBuffer)
            {
                vkCmdBindIndexBuffer(commandBuffer, ib, 0, batch.mesh->indexType);
                boundIndexBuffer = ib;
            }

            const VkDescriptorSet materialSet = batch.material->getDescriptorSet(renderContext.currentFrame);
            if (materialSet != boundMaterialSet)
            {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                        1, 1, &materialSet, 0, nullptr);
                boundMaterialSet = materialSet;
            }

            const ModelPushConstant pushConstant{.baseInstance = batch.firstInstance};
            vkCmdPushConstants(commandBuffer, pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(ModelPushConstant), &pushConstant);

            profiling::cmdDrawIndexed(commandBuffer, batch.mesh->indicesCount, batch.instanceCount, 0, 0, 0);
        }
    };

    drawBatches(data.drawBatches);
}

std::vector<IRenderGraphPass::RenderPassExecution> GBufferRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution execution{};
    execution.renderArea.offset = {0, 0};
    execution.renderArea.extent = m_extent;
    execution.colorFormats = m_colorFormats;
    execution.depthFormat = m_depthFormat;

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

    VkRenderingAttachmentInfo tangentAnisoColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    tangentAnisoColor.imageView = m_tangentAnisoRenderTargets[renderContext.currentImageIndex]->vkImageView();
    tangentAnisoColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    tangentAnisoColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    tangentAnisoColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    tangentAnisoColor.clearValue = m_clearValues[4];

    VkRenderingAttachmentInfo emissiveColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    emissiveColor.imageView = m_emissiveRenderTargets[renderContext.currentImageIndex]->vkImageView();
    emissiveColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    emissiveColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    emissiveColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    emissiveColor.clearValue = m_clearValues[5];

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView = m_depthRenderTarget->vkImageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue = m_clearValues[6];

    execution.colorsRenderingItems = {normalColor, albedoColor, materialColor, objectColor, tangentAnisoColor, emissiveColor};
    execution.depthRenderingItem = depthAttachment;

    execution.targets[m_normalTextureHandlers[renderContext.currentImageIndex]] = m_normalRenderTargets[renderContext.currentImageIndex];
    execution.targets[m_albedoTextureHandlers[renderContext.currentImageIndex]] = m_albedoRenderTargets[renderContext.currentImageIndex];
    execution.targets[m_materialTextureHandlers[renderContext.currentImageIndex]] = m_materialRenderTargets[renderContext.currentImageIndex];
    execution.targets[m_tangentAnisoTextureHandlers[renderContext.currentImageIndex]] = m_tangentAnisoRenderTargets[renderContext.currentImageIndex];
    execution.targets[m_emissiveTextureHandlers[renderContext.currentImageIndex]] = m_emissiveRenderTargets[renderContext.currentImageIndex];
    execution.targets[m_objectIdTextureHandler] = m_objectIdRenderTarget;
    execution.targets[m_depthTextureHandler] = m_depthRenderTarget;

    return {execution};
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

    const int imageCount = static_cast<int>(core::VulkanContext::getContext()->getSwapchain()->getImages().size());
    m_normalRenderTargets.resize(imageCount);
    m_albedoRenderTargets.resize(imageCount);
    m_materialRenderTargets.resize(imageCount);
    m_tangentAnisoRenderTargets.resize(imageCount);
    m_emissiveRenderTargets.resize(imageCount);

    for (int imageIndex = 0; imageIndex < imageCount; ++imageIndex)
    {
        m_normalRenderTargets[imageIndex] = storage.getTexture(m_normalTextureHandlers[imageIndex]);
        m_albedoRenderTargets[imageIndex] = storage.getTexture(m_albedoTextureHandlers[imageIndex]);
        m_materialRenderTargets[imageIndex] = storage.getTexture(m_materialTextureHandlers[imageIndex]);
        m_tangentAnisoRenderTargets[imageIndex] = storage.getTexture(m_tangentAnisoTextureHandlers[imageIndex]);
        m_emissiveRenderTargets[imageIndex] = storage.getTexture(m_emissiveTextureHandlers[imageIndex]);
    }
}

void GBufferRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    const VkFormat hdrImageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    const VkFormat tangentAnisoFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    m_colorFormats = {hdrImageFormat, hdrImageFormat, hdrImageFormat, VK_FORMAT_R32_UINT, tangentAnisoFormat, hdrImageFormat};
    m_depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());

    RGPTextureDescription normalTextureDescription{hdrImageFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription albedoTextureDescription{hdrImageFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription materialTextureDescription{hdrImageFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription tangentAnisoTextureDescription{tangentAnisoFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription emissiveTextureDescription{hdrImageFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription objectIdTextureDescription{VK_FORMAT_R32_UINT, RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription depthTextureDescription{m_depthFormat, RGPTextureUsage::DEPTH_STENCIL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};

    normalTextureDescription.setCustomExtentFunction([this] { return m_extent; });
    albedoTextureDescription.setCustomExtentFunction([this] { return m_extent; });
    materialTextureDescription.setCustomExtentFunction([this] { return m_extent; });
    tangentAnisoTextureDescription.setCustomExtentFunction([this] { return m_extent; });
    emissiveTextureDescription.setCustomExtentFunction([this] { return m_extent; });
    objectIdTextureDescription.setCustomExtentFunction([this] { return m_extent; });
    depthTextureDescription.setCustomExtentFunction([this] { return m_extent; });

    objectIdTextureDescription.setDebugName("__ELIX_OBJECT_ID_GBUFFER_TEXTURE__");
    depthTextureDescription.setDebugName("__ELIX_DEPTH_GBUFFER_TEXTURE__");

    m_objectIdTextureHandler = builder.createTexture(objectIdTextureDescription);
    builder.write(m_objectIdTextureHandler, RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC);

    m_depthTextureHandler = builder.createTexture(depthTextureDescription);
    builder.write(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);

    const int imageCount = static_cast<int>(core::VulkanContext::getContext()->getSwapchain()->getImages().size());
    for (int imageIndex = 0; imageIndex < imageCount; ++imageIndex)
    {
        normalTextureDescription.setDebugName("__ELIX_NORMAL_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");
        albedoTextureDescription.setDebugName("__ELIX_ALBEDO_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");
        materialTextureDescription.setDebugName("__ELIX_MATERIAL_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");
        tangentAnisoTextureDescription.setDebugName("__ELIX_TANGENT_ANISO_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");
        emissiveTextureDescription.setDebugName("__ELIX_EMISSIVE_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");

        const auto normalTexture = builder.createTexture(normalTextureDescription);
        const auto albedoTexture = builder.createTexture(albedoTextureDescription);
        const auto materialTexture = builder.createTexture(materialTextureDescription);
        const auto tangentAnisoTexture = builder.createTexture(tangentAnisoTextureDescription);
        const auto emissiveTexture = builder.createTexture(emissiveTextureDescription);

        m_normalTextureHandlers.push_back(normalTexture);
        m_albedoTextureHandlers.push_back(albedoTexture);
        m_materialTextureHandlers.push_back(materialTexture);
        m_tangentAnisoTextureHandlers.push_back(tangentAnisoTexture);
        m_emissiveTextureHandlers.push_back(emissiveTexture);

        builder.write(normalTexture, RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(albedoTexture, RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(materialTexture, RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(tangentAnisoTexture, RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(emissiveTexture, RGPTextureUsage::COLOR_ATTACHMENT);
    }
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
