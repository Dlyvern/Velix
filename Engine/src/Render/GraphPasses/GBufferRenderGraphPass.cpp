#include "Engine/Render/GraphPasses/GBufferRenderGraphPass.hpp"

#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"

#include <array>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
    VkSampleCountFlagBits toSampleCountFlagBits(uint32_t sampleCount)
    {
        switch (sampleCount)
        {
        case 2u:
            return VK_SAMPLE_COUNT_2_BIT;
        case 4u:
            return VK_SAMPLE_COUNT_4_BIT;
        case 8u:
            return VK_SAMPLE_COUNT_8_BIT;
        case 16u:
            return VK_SAMPLE_COUNT_16_BIT;
        default:
            return VK_SAMPLE_COUNT_1_BIT;
        }
    }

    VkSampleCountFlagBits clampToSupportedSampleCount(VkSampleCountFlagBits requestedSamples)
    {
        const auto properties = core::VulkanContext::getContext()->getPhysicalDevicePoperties();
        const VkSampleCountFlags supportedCounts = properties.limits.framebufferColorSampleCounts &
                                                   properties.limits.framebufferDepthSampleCounts;

        if (requestedSamples == VK_SAMPLE_COUNT_16_BIT && (supportedCounts & VK_SAMPLE_COUNT_16_BIT))
            return VK_SAMPLE_COUNT_16_BIT;
        if (requestedSamples == VK_SAMPLE_COUNT_8_BIT && (supportedCounts & VK_SAMPLE_COUNT_8_BIT))
            return VK_SAMPLE_COUNT_8_BIT;
        if (requestedSamples == VK_SAMPLE_COUNT_4_BIT && (supportedCounts & VK_SAMPLE_COUNT_4_BIT))
            return VK_SAMPLE_COUNT_4_BIT;
        if (requestedSamples == VK_SAMPLE_COUNT_2_BIT && (supportedCounts & VK_SAMPLE_COUNT_2_BIT))
            return VK_SAMPLE_COUNT_2_BIT;

        if (supportedCounts & VK_SAMPLE_COUNT_16_BIT)
            return VK_SAMPLE_COUNT_16_BIT;
        if (supportedCounts & VK_SAMPLE_COUNT_8_BIT)
            return VK_SAMPLE_COUNT_8_BIT;
        if (supportedCounts & VK_SAMPLE_COUNT_4_BIT)
            return VK_SAMPLE_COUNT_4_BIT;
        if (supportedCounts & VK_SAMPLE_COUNT_2_BIT)
            return VK_SAMPLE_COUNT_2_BIT;

        return VK_SAMPLE_COUNT_1_BIT;
    }

    VkSampleCountFlagBits resolveConfiguredMsaaSampleCount()
    {
        const auto &settings = RenderQualitySettings::getInstance();
        const VkSampleCountFlagBits requestedSamples = toSampleCountFlagBits(settings.getMSAASampleCount());
        if (requestedSamples == VK_SAMPLE_COUNT_1_BIT)
            return VK_SAMPLE_COUNT_1_BIT;

        return clampToSupportedSampleCount(requestedSamples);
    }
}

struct ModelPushConstant
{
    uint32_t baseInstance{0};
    uint32_t padding[3]{0, 0, 0};
};

GBufferRenderGraphPass::GBufferRenderGraphPass()
{
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    m_clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[3].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[4].color = {{0.5f, 0.5f, 0.5f, 0.0f}}; // encoded tangent + optional aniso
    m_clearValues[5].depthStencil = {1.0f, 0};

    this->setDebugName("GBuffer render graph pass");

    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void GBufferRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                                    const RenderGraphPassContext &renderContext)
{
    const VkSampleCountFlagBits desiredMsaaSamples = resolveConfiguredMsaaSampleCount();
    if (desiredMsaaSamples != m_requestedMsaaSamples)
    {
        m_requestedMsaaSamples = desiredMsaaSamples;

        auto updateDescriptionSampleCount = [this](const RGPResourceHandler &handler, VkSampleCountFlagBits sampleCount)
        {
            if (!m_resourcesBuilder)
                return;

            auto *description = m_resourcesBuilder->getTextureDescriptionMutable(handler);
            if (description)
                description->setSampleCount(sampleCount);
        };

        updateDescriptionSampleCount(m_depthMsaaTextureHandler, m_requestedMsaaSamples);
        updateDescriptionSampleCount(m_objectIdMsaaTextureHandler, m_requestedMsaaSamples);

        for (size_t index = 0; index < m_normalMsaaTextureHandlers.size(); ++index)
        {
            updateDescriptionSampleCount(m_normalMsaaTextureHandlers[index], m_requestedMsaaSamples);
            updateDescriptionSampleCount(m_albedoMsaaTextureHandlers[index], m_requestedMsaaSamples);
            updateDescriptionSampleCount(m_materialMsaaTextureHandlers[index], m_requestedMsaaSamples);
            updateDescriptionSampleCount(m_tangentAnisoMsaaTextureHandlers[index], m_requestedMsaaSamples);
        }

        requestRecompilation();
    }

    const uint32_t executionCount = m_useMsaa ? 2u : 1u;
    if (m_currentExecutionIndex >= executionCount)
        return;

    const ExecutionVariant executionVariant =
        (m_useMsaa && m_currentExecutionIndex == static_cast<uint32_t>(ExecutionVariant::OBJECT_ONLY))
            ? ExecutionVariant::OBJECT_ONLY
            : ExecutionVariant::MAIN;
    ++m_currentExecutionIndex;

    const bool isObjectOnlyExecution = executionVariant == ExecutionVariant::OBJECT_ONLY;
    const VkSampleCountFlagBits rasterizationSamples = isObjectOnlyExecution ? VK_SAMPLE_COUNT_1_BIT : m_activeMsaaSamples;
    const bool canRecordOcclusionQueries =
        !isObjectOnlyExecution &&
        data.enableOcclusionCulling &&
        data.occlusionQueryPool != VK_NULL_HANDLE;
    uint32_t occlusionQueryCursor = 0u;

    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    const auto drawBatches = [&](const std::vector<DrawBatch> &batches, bool probePass)
    {
        if (batches.empty())
            return;

        const auto pipelineLayout = EngineShaderFamilies::meshShaderFamily.pipelineLayout;

        // Build one key per skinned variant — hoisted out of the loop to avoid
        // constructing + heap-copying colorFormats once per draw call.
        auto makeKey = [&](bool skinned) -> GraphicsPipelineKey
        {
            GraphicsPipelineKey k{};
            k.shader = skinned ? ShaderId::GBufferSkinned : ShaderId::GBufferStatic;
            k.cull = CullMode::Back;
            k.depthTest = true;
            k.depthWrite = !probePass;
            k.depthCompare = probePass ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
            k.polygonMode = VK_POLYGON_MODE_FILL;
            k.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            k.rasterizationSamples = rasterizationSamples;
            k.gbufferOutputMode = isObjectOnlyExecution ? GBufferOutputMode::ObjectOnly : GBufferOutputMode::Full;
            k.colorFormats = m_colorFormats;
            k.depthFormat = m_depthFormat;
            return k;
        };

        // Resolve both pipelines once before the loop.
        const VkPipeline staticPipeline = GraphicsPipelineManager::getOrCreate(makeKey(false));
        const VkPipeline skinnedPipeline = GraphicsPipelineManager::getOrCreate(makeKey(true));

        // Per-draw state cache to skip redundant Vulkan calls.
        VkPipeline       boundPipeline      = VK_NULL_HANDLE;
        VkBuffer         boundVertexBuffer  = VK_NULL_HANDLE;
        VkBuffer         boundIndexBuffer   = VK_NULL_HANDLE;
        VkDescriptorSet  boundMaterialSet   = VK_NULL_HANDLE;

        for (const auto &batch : batches)
        {
            if (!batch.mesh || !batch.material || batch.instanceCount == 0)
                continue;

            if (probePass && (!canRecordOcclusionQueries || !batch.runOcclusionQuery || occlusionQueryCursor >= data.occlusionQueryKeys.size()))
                continue;

            const VkPipeline batchPipeline = batch.skinned ? skinnedPipeline : staticPipeline;
            if (batchPipeline != boundPipeline)
            {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batchPipeline);
                boundPipeline = batchPipeline;
                // Bind constant descriptor sets AFTER the pipeline — required by Vulkan
                // spec / driver best-practices to avoid sets being silently invalidated.
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                        0, 1, &data.cameraDescriptorSet, 0, nullptr);
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                        2, 1, &data.perObjectDescriptorSet, 0, nullptr);
                boundMaterialSet = VK_NULL_HANDLE; // force set 1 rebind after pipeline switch
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

            const VkDescriptorSet matSet = batch.material->getDescriptorSet(renderContext.currentFrame);
            if (matSet != boundMaterialSet)
            {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                        1, 1, &matSet, 0, nullptr);
                boundMaterialSet = matSet;
            }

            ModelPushConstant modelPushConstant{.baseInstance = batch.firstInstance};
            vkCmdPushConstants(commandBuffer, pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(ModelPushConstant), &modelPushConstant);

            const bool beginOcclusionQuery =
                canRecordOcclusionQueries &&
                batch.runOcclusionQuery &&
                occlusionQueryCursor < data.occlusionQueryKeys.size();
            if (beginOcclusionQuery)
                vkCmdBeginQuery(commandBuffer, data.occlusionQueryPool, data.occlusionQueryBase + occlusionQueryCursor, 0u);

            profiling::cmdDrawIndexed(commandBuffer, batch.mesh->indicesCount, batch.instanceCount, 0, 0, 0);

            if (beginOcclusionQuery)
            {
                vkCmdEndQuery(commandBuffer, data.occlusionQueryPool, data.occlusionQueryBase + occlusionQueryCursor);
                ++occlusionQueryCursor;
            }
        }
    };

    drawBatches(data.drawBatches, false);

    if (!isObjectOnlyExecution)
        drawBatches(data.occlusionProbeBatches, true);
}

std::vector<IRenderGraphPass::RenderPassExecution> GBufferRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    m_currentExecutionIndex = 0;

    IRenderGraphPass::RenderPassExecution mainExecution;
    mainExecution.renderArea.offset = {0, 0};
    mainExecution.renderArea.extent = m_extent;
    mainExecution.colorFormats = m_colorFormats;
    mainExecution.depthFormat = m_depthFormat;

    const bool resolveMsaa = m_useMsaa;

    const RenderTarget *normalTarget = resolveMsaa ? m_normalMsaaRenderTargets[renderContext.currentImageIndex] : m_normalRenderTargets[renderContext.currentImageIndex];
    const RenderTarget *albedoTarget = resolveMsaa ? m_albedoMsaaRenderTargets[renderContext.currentImageIndex] : m_albedoRenderTargets[renderContext.currentImageIndex];
    const RenderTarget *materialTarget = resolveMsaa ? m_materialMsaaRenderTargets[renderContext.currentImageIndex] : m_materialRenderTargets[renderContext.currentImageIndex];
    const RenderTarget *tangentAnisoTarget = resolveMsaa ? m_tangentAnisoMsaaRenderTargets[renderContext.currentImageIndex] : m_tangentAnisoRenderTargets[renderContext.currentImageIndex];
    const RenderTarget *objectTarget = resolveMsaa ? m_objectIdMsaaRenderTarget : m_objectIdRenderTarget;
    const RenderTarget *depthTarget = resolveMsaa ? m_depthMsaaRenderTarget : m_depthRenderTarget;

    VkRenderingAttachmentInfo normalColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    normalColor.imageView = normalTarget->vkImageView();
    normalColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    normalColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    normalColor.storeOp = resolveMsaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    normalColor.clearValue = m_clearValues[0];
    if (resolveMsaa)
    {
        normalColor.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        normalColor.resolveImageView = m_normalRenderTargets[renderContext.currentImageIndex]->vkImageView();
        normalColor.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo albedoColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    albedoColor.imageView = albedoTarget->vkImageView();
    albedoColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    albedoColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    albedoColor.storeOp = resolveMsaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    albedoColor.clearValue = m_clearValues[1];
    if (resolveMsaa)
    {
        albedoColor.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        albedoColor.resolveImageView = m_albedoRenderTargets[renderContext.currentImageIndex]->vkImageView();
        albedoColor.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo materialColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    materialColor.imageView = materialTarget->vkImageView();
    materialColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    materialColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    materialColor.storeOp = resolveMsaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    materialColor.clearValue = m_clearValues[2];
    if (resolveMsaa)
    {
        materialColor.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        materialColor.resolveImageView = m_materialRenderTargets[renderContext.currentImageIndex]->vkImageView();
        materialColor.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo objectColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    objectColor.imageView = objectTarget->vkImageView();
    objectColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    objectColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    objectColor.storeOp = resolveMsaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    objectColor.clearValue = m_clearValues[3];

    VkRenderingAttachmentInfo tangentAnisoColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    tangentAnisoColor.imageView = tangentAnisoTarget->vkImageView();
    tangentAnisoColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    tangentAnisoColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    tangentAnisoColor.storeOp = resolveMsaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    tangentAnisoColor.clearValue = m_clearValues[4];
    if (resolveMsaa)
    {
        tangentAnisoColor.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        tangentAnisoColor.resolveImageView = m_tangentAnisoRenderTargets[renderContext.currentImageIndex]->vkImageView();
        tangentAnisoColor.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo depthAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAtt.imageView = depthTarget->vkImageView();
    depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp = resolveMsaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    depthAtt.clearValue = m_clearValues[5];

    mainExecution.colorsRenderingItems = {normalColor, albedoColor, materialColor, objectColor, tangentAnisoColor};
    mainExecution.depthRenderingItem = depthAtt;
    mainExecution.rasterizationSamples = resolveMsaa ? m_activeMsaaSamples : VK_SAMPLE_COUNT_1_BIT;

    mainExecution.targets[m_normalTextureHandlers[renderContext.currentImageIndex]] = m_normalRenderTargets[renderContext.currentImageIndex];
    mainExecution.targets[m_albedoTextureHandlers[renderContext.currentImageIndex]] = m_albedoRenderTargets[renderContext.currentImageIndex];
    mainExecution.targets[m_materialTextureHandlers[renderContext.currentImageIndex]] = m_materialRenderTargets[renderContext.currentImageIndex];
    mainExecution.targets[m_tangentAnisoTextureHandlers[renderContext.currentImageIndex]] = m_tangentAnisoRenderTargets[renderContext.currentImageIndex];

    if (resolveMsaa)
    {
        mainExecution.targets[m_normalMsaaTextureHandlers[renderContext.currentImageIndex]] = m_normalMsaaRenderTargets[renderContext.currentImageIndex];
        mainExecution.targets[m_albedoMsaaTextureHandlers[renderContext.currentImageIndex]] = m_albedoMsaaRenderTargets[renderContext.currentImageIndex];
        mainExecution.targets[m_materialMsaaTextureHandlers[renderContext.currentImageIndex]] = m_materialMsaaRenderTargets[renderContext.currentImageIndex];
        mainExecution.targets[m_tangentAnisoMsaaTextureHandlers[renderContext.currentImageIndex]] = m_tangentAnisoMsaaRenderTargets[renderContext.currentImageIndex];
        mainExecution.targets[m_objectIdMsaaTextureHandler] = m_objectIdMsaaRenderTarget;
        mainExecution.targets[m_depthMsaaTextureHandler] = m_depthMsaaRenderTarget;
    }
    else
    {
        mainExecution.targets[m_objectIdTextureHandler] = m_objectIdRenderTarget;
        mainExecution.targets[m_depthTextureHandler] = m_depthRenderTarget;
    }

    if (!resolveMsaa)
        return {mainExecution};

    IRenderGraphPass::RenderPassExecution objectExecution;
    objectExecution.renderArea.offset = {0, 0};
    objectExecution.renderArea.extent = m_extent;
    objectExecution.colorFormats = m_colorFormats;
    objectExecution.depthFormat = m_depthFormat;
    objectExecution.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkRenderingAttachmentInfo objectPassNormal{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    objectPassNormal.imageView = m_normalRenderTargets[renderContext.currentImageIndex]->vkImageView();
    objectPassNormal.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    objectPassNormal.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    objectPassNormal.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo objectPassAlbedo{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    objectPassAlbedo.imageView = m_albedoRenderTargets[renderContext.currentImageIndex]->vkImageView();
    objectPassAlbedo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    objectPassAlbedo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    objectPassAlbedo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo objectPassMaterial{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    objectPassMaterial.imageView = m_materialRenderTargets[renderContext.currentImageIndex]->vkImageView();
    objectPassMaterial.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    objectPassMaterial.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    objectPassMaterial.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo objectPassColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    objectPassColor.imageView = m_objectIdRenderTarget->vkImageView();
    objectPassColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    objectPassColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    objectPassColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    objectPassColor.clearValue = m_clearValues[3];

    VkRenderingAttachmentInfo objectPassTangentAniso{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    objectPassTangentAniso.imageView = m_tangentAnisoRenderTargets[renderContext.currentImageIndex]->vkImageView();
    objectPassTangentAniso.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    objectPassTangentAniso.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    objectPassTangentAniso.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo objectPassDepth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    objectPassDepth.imageView = m_depthRenderTarget->vkImageView();
    objectPassDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    objectPassDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    objectPassDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    objectPassDepth.clearValue = m_clearValues[5];

    objectExecution.colorsRenderingItems = {objectPassNormal, objectPassAlbedo, objectPassMaterial, objectPassColor, objectPassTangentAniso};
    objectExecution.depthRenderingItem = objectPassDepth;
    objectExecution.targets[m_normalTextureHandlers[renderContext.currentImageIndex]] = m_normalRenderTargets[renderContext.currentImageIndex];
    objectExecution.targets[m_albedoTextureHandlers[renderContext.currentImageIndex]] = m_albedoRenderTargets[renderContext.currentImageIndex];
    objectExecution.targets[m_materialTextureHandlers[renderContext.currentImageIndex]] = m_materialRenderTargets[renderContext.currentImageIndex];
    objectExecution.targets[m_tangentAnisoTextureHandlers[renderContext.currentImageIndex]] = m_tangentAnisoRenderTargets[renderContext.currentImageIndex];
    objectExecution.targets[m_objectIdTextureHandler] = m_objectIdRenderTarget;
    objectExecution.targets[m_depthTextureHandler] = m_depthRenderTarget;

    return {mainExecution, objectExecution};
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
    m_activeMsaaSamples = m_requestedMsaaSamples;
    m_useMsaa = m_activeMsaaSamples != VK_SAMPLE_COUNT_1_BIT;

    m_depthRenderTarget = storage.getTexture(m_depthTextureHandler);
    m_objectIdRenderTarget = storage.getTexture(m_objectIdTextureHandler);
    m_depthMsaaRenderTarget = storage.getTexture(m_depthMsaaTextureHandler);
    m_objectIdMsaaRenderTarget = storage.getTexture(m_objectIdMsaaTextureHandler);

    int imageCount = core::VulkanContext::getContext()->getSwapchain()->getImages().size();

    m_normalRenderTargets.resize(imageCount);
    m_albedoRenderTargets.resize(imageCount);
    m_materialRenderTargets.resize(imageCount);
    m_tangentAnisoRenderTargets.resize(imageCount);
    m_normalMsaaRenderTargets.resize(imageCount);
    m_albedoMsaaRenderTargets.resize(imageCount);
    m_materialMsaaRenderTargets.resize(imageCount);
    m_tangentAnisoMsaaRenderTargets.resize(imageCount);

    for (int imageIndex = 0; imageIndex < imageCount; ++imageIndex)
    {
        m_normalRenderTargets[imageIndex] = storage.getTexture(m_normalTextureHandlers[imageIndex]);
        m_albedoRenderTargets[imageIndex] = storage.getTexture(m_albedoTextureHandlers[imageIndex]);
        m_materialRenderTargets[imageIndex] = storage.getTexture(m_materialTextureHandlers[imageIndex]);
        m_tangentAnisoRenderTargets[imageIndex] = storage.getTexture(m_tangentAnisoTextureHandlers[imageIndex]);

        m_normalMsaaRenderTargets[imageIndex] = storage.getTexture(m_normalMsaaTextureHandlers[imageIndex]);
        m_albedoMsaaRenderTargets[imageIndex] = storage.getTexture(m_albedoMsaaTextureHandlers[imageIndex]);
        m_materialMsaaRenderTargets[imageIndex] = storage.getTexture(m_materialMsaaTextureHandlers[imageIndex]);
        m_tangentAnisoMsaaRenderTargets[imageIndex] = storage.getTexture(m_tangentAnisoMsaaTextureHandlers[imageIndex]);
    }
}

void GBufferRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_resourcesBuilder = &builder;
    m_requestedMsaaSamples = resolveConfiguredMsaaSampleCount();
    m_activeMsaaSamples = m_requestedMsaaSamples;
    m_useMsaa = m_activeMsaaSamples != VK_SAMPLE_COUNT_1_BIT;

    auto hdrImageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    auto tangentAnisoFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    m_colorFormats = {hdrImageFormat, hdrImageFormat, hdrImageFormat, VK_FORMAT_R32_UINT, tangentAnisoFormat};
    m_depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());

    RGPTextureDescription normalTextureDescription{hdrImageFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription albedoTextureDescription{hdrImageFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription materialTextureDescription{hdrImageFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription tangentAnisoTextureDescription{tangentAnisoFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription objectIdTextureDescription{VK_FORMAT_R32_UINT, RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription depthTextureDescription{m_depthFormat, RGPTextureUsage::DEPTH_STENCIL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};

    RGPTextureDescription normalMsaaTextureDescription{hdrImageFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    RGPTextureDescription albedoMsaaTextureDescription{hdrImageFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    RGPTextureDescription materialMsaaTextureDescription{hdrImageFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    RGPTextureDescription tangentAnisoMsaaTextureDescription{tangentAnisoFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    RGPTextureDescription objectIdMsaaTextureDescription{VK_FORMAT_R32_UINT, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    RGPTextureDescription depthMsaaTextureDescription{m_depthFormat, RGPTextureUsage::DEPTH_STENCIL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    normalTextureDescription.setCustomExtentFunction([this]
                                                     { return m_extent; });
    albedoTextureDescription.setCustomExtentFunction([this]
                                                     { return m_extent; });
    materialTextureDescription.setCustomExtentFunction([this]
                                                       { return m_extent; });
    tangentAnisoTextureDescription.setCustomExtentFunction([this]
                                                           { return m_extent; });
    normalMsaaTextureDescription.setCustomExtentFunction([this]
                                                         { return m_extent; });
    albedoMsaaTextureDescription.setCustomExtentFunction([this]
                                                         { return m_extent; });
    materialMsaaTextureDescription.setCustomExtentFunction([this]
                                                           { return m_extent; });
    tangentAnisoMsaaTextureDescription.setCustomExtentFunction([this]
                                                               { return m_extent; });

    objectIdTextureDescription.setDebugName("__ELIX_OBJECT_ID_GBUFFER_TEXTURE__");
    objectIdTextureDescription.setCustomExtentFunction([this]
                                                       { return m_extent; });
    objectIdMsaaTextureDescription.setDebugName("__ELIX_OBJECT_ID_GBUFFER_MSAA_TEXTURE__");
    objectIdMsaaTextureDescription.setCustomExtentFunction([this]
                                                           { return m_extent; });

    m_objectIdTextureHandler = builder.createTexture(objectIdTextureDescription);
    builder.write(m_objectIdTextureHandler, RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC);
    objectIdMsaaTextureDescription.setSampleCount(m_requestedMsaaSamples);
    m_objectIdMsaaTextureHandler = builder.createTexture(objectIdMsaaTextureDescription);
    builder.write(m_objectIdMsaaTextureHandler, RGPTextureUsage::COLOR_ATTACHMENT);

    depthTextureDescription.setDebugName("__ELIX_DEPTH_GBUFFER_TEXTURE__");
    depthTextureDescription.setCustomExtentFunction([this]
                                                    { return m_extent; });
    depthMsaaTextureDescription.setDebugName("__ELIX_DEPTH_GBUFFER_MSAA_TEXTURE__");
    depthMsaaTextureDescription.setCustomExtentFunction([this]
                                                        { return m_extent; });

    m_depthTextureHandler = builder.createTexture(depthTextureDescription);
    builder.write(m_depthTextureHandler, renderGraph::RGPTextureUsage::DEPTH_STENCIL);
    depthMsaaTextureDescription.setSampleCount(m_requestedMsaaSamples);
    m_depthMsaaTextureHandler = builder.createTexture(depthMsaaTextureDescription);
    builder.write(m_depthMsaaTextureHandler, renderGraph::RGPTextureUsage::DEPTH_STENCIL);

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        normalTextureDescription.setDebugName("__ELIX_NORMAL_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");
        albedoTextureDescription.setDebugName("__ELIX_ALBEDO_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");
        materialTextureDescription.setDebugName("__ELIX_MATERIAL_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");
        tangentAnisoTextureDescription.setDebugName("__ELIX_TANGENT_ANISO_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");
        normalMsaaTextureDescription.setDebugName("__ELIX_NORMAL_GBUFFER_MSAA_TEXTURE_" + std::to_string(imageIndex) + "__");
        albedoMsaaTextureDescription.setDebugName("__ELIX_ALBEDO_GBUFFER_MSAA_TEXTURE_" + std::to_string(imageIndex) + "__");
        materialMsaaTextureDescription.setDebugName("__ELIX_MATERIAL_GBUFFER_MSAA_TEXTURE_" + std::to_string(imageIndex) + "__");
        tangentAnisoMsaaTextureDescription.setDebugName("__ELIX_TANGENT_ANISO_GBUFFER_MSAA_TEXTURE_" + std::to_string(imageIndex) + "__");
        normalMsaaTextureDescription.setSampleCount(m_requestedMsaaSamples);
        albedoMsaaTextureDescription.setSampleCount(m_requestedMsaaSamples);
        materialMsaaTextureDescription.setSampleCount(m_requestedMsaaSamples);
        tangentAnisoMsaaTextureDescription.setSampleCount(m_requestedMsaaSamples);

        auto normalTexture = builder.createTexture(normalTextureDescription);
        auto albedoTexture = builder.createTexture(albedoTextureDescription);
        auto materialTexture = builder.createTexture(materialTextureDescription);
        auto tangentAnisoTexture = builder.createTexture(tangentAnisoTextureDescription);
        auto normalMsaaTexture = builder.createTexture(normalMsaaTextureDescription);
        auto albedoMsaaTexture = builder.createTexture(albedoMsaaTextureDescription);
        auto materialMsaaTexture = builder.createTexture(materialMsaaTextureDescription);
        auto tangentAnisoMsaaTexture = builder.createTexture(tangentAnisoMsaaTextureDescription);

        m_normalTextureHandlers.push_back(normalTexture);
        m_albedoTextureHandlers.push_back(albedoTexture);
        m_materialTextureHandlers.push_back(materialTexture);
        m_tangentAnisoTextureHandlers.push_back(tangentAnisoTexture);
        m_normalMsaaTextureHandlers.push_back(normalMsaaTexture);
        m_albedoMsaaTextureHandlers.push_back(albedoMsaaTexture);
        m_materialMsaaTextureHandlers.push_back(materialMsaaTexture);
        m_tangentAnisoMsaaTextureHandlers.push_back(tangentAnisoMsaaTexture);

        builder.write(normalTexture, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(albedoTexture, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(materialTexture, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(tangentAnisoTexture, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(normalMsaaTexture, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(albedoMsaaTexture, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(materialMsaaTexture, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(tangentAnisoMsaaTexture, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
    }
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
