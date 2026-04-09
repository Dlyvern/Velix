#include "Engine/Render/GraphPasses/GBufferRenderGraphPass.hpp"

#include "Core/Logger.hpp"
#include "Core/VulkanContext.hpp"

#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
    uint32_t sampleCountToInt(VkSampleCountFlagBits sampleCount)
    {
        switch (sampleCount)
        {
        case VK_SAMPLE_COUNT_2_BIT:
            return 2u;
        case VK_SAMPLE_COUNT_4_BIT:
            return 4u;
        case VK_SAMPLE_COUNT_8_BIT:
            return 8u;
        case VK_SAMPLE_COUNT_16_BIT:
            return 16u;
        case VK_SAMPLE_COUNT_32_BIT:
            return 32u;
        case VK_SAMPLE_COUNT_64_BIT:
            return 64u;
        case VK_SAMPLE_COUNT_1_BIT:
        default:
            return 1u;
        }
    }

    VkSampleCountFlagBits resolveGBufferSampleCount()
    {
        const auto context = core::VulkanContext::getContext();
        const auto requested = RenderQualitySettings::getInstance().getRequestedMsaaSampleCount();

        if (requested == VK_SAMPLE_COUNT_1_BIT)
            return VK_SAMPLE_COUNT_1_BIT;

        const auto clamped = context->clampSupportedSampleCount(requested);
        if (clamped != requested)
        {
            VX_ENGINE_WARNING_STREAM("GBuffer MSAA requested "
                                     << sampleCountToInt(requested)
                                     << "x, but the adapter only supports up to "
                                     << sampleCountToInt(clamped)
                                     << "x for color+depth framebuffers. Clamping.\n");
        }

        if (!context->supportsSampleZeroDepthResolve())
        {
            VX_ENGINE_WARNING_STREAM("GBuffer MSAA requested "
                                     << sampleCountToInt(requested)
                                     << "x, but depth resolve mode SAMPLE_ZERO is unavailable. Falling back to Off.\n");
            return VK_SAMPLE_COUNT_1_BIT;
        }

        return clamped;
    }

    constexpr float kSharedDepthBiasConstantFactor = -0.25f;
    constexpr float kSharedDepthBiasSlopeFactor = 0.0f;

    struct ModelPushConstant
    {
        uint32_t baseInstance{0};
        uint32_t padding[3]{0, 0, 0};
        float time{0.0f};
    };
}

GBufferRenderGraphPass::GBufferRenderGraphPass(bool enableObjectId)
    : m_enableObjectId(enableObjectId)
{
    m_clearValues[0].color = {{0.5f, 0.5f, 1.0f, 1.0f}};
    m_clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[2].color = {{1.0f, 1.0f, 0.0f, 0.0f}};
    m_clearValues[3].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[4].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[5].depthStencil = {1.0f, 0};

    setDebugName("GBuffer render graph pass");
    outputs.normals.setOwner(this);
    outputs.albedo.setOwner(this);
    outputs.material.setOwner(this);
    outputs.emissive.setOwner(this);
    outputs.depth.setOwner(this);
    outputs.objectId.setOwner(this);
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

        // Bindless GBuffer pipeline layout (Set 1 = bindless texture array + material SSBO).
        const auto pipelineLayout = EngineShaderFamilies::bindlessMeshPipelineLayout
                                        ? EngineShaderFamilies::bindlessMeshPipelineLayout
                                        : static_cast<VkPipelineLayout>(
                                              EngineShaderFamilies::meshShaderFamily.pipelineLayout);

        // Bind the global bindless descriptor set once at Set 1 — never rebind per batch.
        if (data.bindlessDescriptorSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                    1, 1, &data.bindlessDescriptorSet, 0, nullptr);

        auto makeKey = [&](bool skinned, bool alphaBlend, bool alphaMask, bool twoSided) -> GraphicsPipelineKey
        {
            GraphicsPipelineKey key{};
            key.shader = skinned ? ShaderId::GBufferSkinned : ShaderId::GBufferStatic;
            key.blend = alphaBlend ? BlendMode::AlphaBlend : BlendMode::None;
            key.cull = twoSided ? CullMode::None : CullMode::Back;
            key.depthTest = true;
            key.polygonMode = VK_POLYGON_MODE_FILL;
            key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            key.pipelineLayout = pipelineLayout;

            // When a depth prepass has already written depth for fully-opaque objects,
            // use LESS_OR_EQUAL + no write to eliminate overdraw while tolerating the tiny
            // floating-point precision delta between two vertex-shader runs (EQUAL causes
            // holes in curved surfaces like spheres where depth values differ by 1 ULP).
            // Alpha-blend and alpha-masked objects skip the depth prepass and still use LESS.
            const bool depthPrepassCovered = m_hasExternalDepth && !alphaBlend && !alphaMask;
            key.depthWrite   = !alphaBlend && !depthPrepassCovered;
            key.depthCompare = depthPrepassCovered ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
            key.depthClampEnable = depthPrepassCovered && core::VulkanContext::getContext()->hasDepthClampSupport();
            key.depthBiasEnable = depthPrepassCovered;
            key.depthBiasConstantFactor = depthPrepassCovered ? kSharedDepthBiasConstantFactor : 0.0f;
            key.depthBiasSlopeFactor = depthPrepassCovered ? kSharedDepthBiasSlopeFactor : 0.0f;
            key.rasterizationSamples = m_rasterizationSamples;
            return key;
        };

        std::unordered_map<GraphicsPipelineKey, VkPipeline, GraphicsPipelineKeyHash> pipelineCache;
        pipelineCache.reserve(8u);

        auto getPipelineForBatch = [&](const DrawBatch &batch) -> VkPipeline
        {
            const uint32_t materialFlags = batch.material ? batch.material->params().flags : 0u;
            const bool alphaBlend = (materialFlags & Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND) != 0u;
            const bool alphaMask  = (materialFlags & Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK)  != 0u;
            const bool twoSided   = (materialFlags & Material::MaterialFlags::EMATERIAL_FLAG_DOUBLE_SIDED) != 0u;

            GraphicsPipelineKey key = makeKey(batch.skinned, alphaBlend, alphaMask, twoSided);

            if (!batch.material->getCustomFragPath().empty())
                key.customFragSpvPath = batch.material->getCustomFragPath();

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

        // If the unified static geometry buffer is available and all static batches are
        // registered in it, bind the unified VB/IB once before the loop to avoid per-batch
        // vertex/index buffer rebinds (the biggest source of CPU draw overhead).
        const bool hasUnifiedGeometry =
            data.unifiedStaticVertexBuffer != VK_NULL_HANDLE &&
            data.unifiedStaticIndexBuffer  != VK_NULL_HANDLE;

        VkBuffer boundUnifiedVB = VK_NULL_HANDLE;
        VkBuffer boundUnifiedIB = VK_NULL_HANDLE;

        VkPipeline boundPipeline = VK_NULL_HANDLE;
        VkBuffer boundVertexBuffer = VK_NULL_HANDLE;
        VkBuffer boundIndexBuffer = VK_NULL_HANDLE;

        const bool hasIndirectBuffer = data.indirectDrawBuffer != VK_NULL_HANDLE;

        uint32_t batchIndex = 0;
        for (const auto &batch : batches)
        {
            const uint32_t thisBatchIdx = batchIndex++;

            if (!batch.mesh || !batch.material)
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
                // Pipeline change resets the active VB/IB tracking so we rebind below.
                boundVertexBuffer = VK_NULL_HANDLE;
                boundIndexBuffer  = VK_NULL_HANDLE;
                boundUnifiedVB    = VK_NULL_HANDLE;
                boundUnifiedIB    = VK_NULL_HANDLE;
            }

            // Use the unified buffer path for static meshes that were registered in it.
            const bool useUnified = hasUnifiedGeometry && !batch.skinned && batch.mesh->inUnifiedBuffer;

            if (useUnified)
            {
                if (data.unifiedStaticVertexBuffer != boundUnifiedVB)
                {
                    const VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &data.unifiedStaticVertexBuffer, &offset);
                    boundUnifiedVB    = data.unifiedStaticVertexBuffer;
                    boundVertexBuffer = VK_NULL_HANDLE; // invalidate per-mesh tracking
                }
                if (data.unifiedStaticIndexBuffer != boundUnifiedIB)
                {
                    vkCmdBindIndexBuffer(commandBuffer, data.unifiedStaticIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    boundUnifiedIB    = data.unifiedStaticIndexBuffer;
                    boundIndexBuffer  = VK_NULL_HANDLE;
                }
            }
            else
            {
                // Per-mesh fallback path (skinned, or not yet in unified buffer).
                if (boundUnifiedVB != VK_NULL_HANDLE || boundUnifiedIB != VK_NULL_HANDLE)
                {
                    // Switched from unified → per-mesh; force rebind.
                    boundVertexBuffer = VK_NULL_HANDLE;
                    boundIndexBuffer  = VK_NULL_HANDLE;
                    boundUnifiedVB    = VK_NULL_HANDLE;
                    boundUnifiedIB    = VK_NULL_HANDLE;
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
            }

            // Per-batch material bind eliminated — the bindless descriptor set at Set 1
            // covers all materials; shaders index via fragMaterialIndex from instance data.

            const ModelPushConstant pushConstant{.baseInstance = batch.firstInstance, .padding = {0,0,0}, .time = data.elapsedTime};
            vkCmdPushConstants(commandBuffer, pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(ModelPushConstant), &pushConstant);

            // When using the unified buffer, supply each mesh's stored vertex/index offsets
            // so the GPU reads from the correct region of the shared buffer.
            const uint32_t firstIndex   = useUnified ? batch.mesh->unifiedFirstIndex   : 0u;
            const int32_t  vertexOffset = useUnified ? batch.mesh->unifiedVertexOffset : 0;

            // GPU-driven path: use the indirect buffer written by the compute culling pass.
            // The GPU may have zeroed instanceCount for culled batches, so this also
            // implicitly handles frustum culling without CPU readback.
            const bool useIndirect = useUnified && hasIndirectBuffer;
            if (useIndirect)
            {
                const VkDeviceSize indirectOffset =
                    static_cast<VkDeviceSize>(thisBatchIdx) * sizeof(VkDrawIndexedIndirectCommand);
                vkCmdDrawIndexedIndirect(commandBuffer, data.indirectDrawBuffer,
                                         indirectOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
            }
            else
            {
                if (batch.instanceCount == 0)
                    continue;
                profiling::cmdDrawIndexed(commandBuffer, batch.mesh->indicesCount, batch.instanceCount,
                                          firstIndex, vertexOffset, 0);
            }
        }
    };

    drawBatches(data.drawBatches);
}

std::vector<IRenderGraphPass::RenderPassExecution> GBufferRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    const bool msaaEnabled = m_rasterizationSamples != VK_SAMPLE_COUNT_1_BIT;

    IRenderGraphPass::RenderPassExecution execution{};
    execution.renderArea.offset = {0, 0};
    execution.renderArea.extent = m_extent;
    execution.colorFormats = m_colorFormats;
    execution.depthFormat = m_depthFormat;
    execution.rasterizationSamples = m_rasterizationSamples;

    VkRenderingAttachmentInfo normalColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    normalColor.imageView = (msaaEnabled ? m_msaaNormalRenderTargets[renderContext.currentImageIndex]
                                         : m_normalRenderTargets[renderContext.currentImageIndex])->vkImageView();
    normalColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    normalColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    normalColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    normalColor.clearValue = m_clearValues[0];
    if (msaaEnabled)
    {
        normalColor.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        normalColor.resolveImageView = m_normalRenderTargets[renderContext.currentImageIndex]->vkImageView();
        normalColor.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo albedoColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    albedoColor.imageView = (msaaEnabled ? m_msaaAlbedoRenderTargets[renderContext.currentImageIndex]
                                         : m_albedoRenderTargets[renderContext.currentImageIndex])->vkImageView();
    albedoColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    albedoColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    albedoColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    albedoColor.clearValue = m_clearValues[1];
    if (msaaEnabled)
    {
        albedoColor.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        albedoColor.resolveImageView = m_albedoRenderTargets[renderContext.currentImageIndex]->vkImageView();
        albedoColor.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo materialColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    materialColor.imageView = (msaaEnabled ? m_msaaMaterialRenderTargets[renderContext.currentImageIndex]
                                           : m_materialRenderTargets[renderContext.currentImageIndex])->vkImageView();
    materialColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    materialColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    materialColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    materialColor.clearValue = m_clearValues[2];
    if (msaaEnabled)
    {
        materialColor.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        materialColor.resolveImageView = m_materialRenderTargets[renderContext.currentImageIndex]->vkImageView();
        materialColor.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo emissiveColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    emissiveColor.imageView = (msaaEnabled ? m_msaaEmissiveRenderTargets[renderContext.currentImageIndex]
                                           : m_emissiveRenderTargets[renderContext.currentImageIndex])->vkImageView();
    emissiveColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    emissiveColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    emissiveColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    emissiveColor.clearValue = m_clearValues[3];
    if (msaaEnabled)
    {
        emissiveColor.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        emissiveColor.resolveImageView = m_emissiveRenderTargets[renderContext.currentImageIndex]->vkImageView();
        emissiveColor.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo objectColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    const auto *objectAttachmentTarget = msaaEnabled ? m_msaaObjectIdRenderTarget : m_objectIdRenderTarget;
    objectColor.imageView = objectAttachmentTarget ? objectAttachmentTarget->vkImageView() : VK_NULL_HANDLE;
    objectColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    objectColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    objectColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    objectColor.clearValue = m_clearValues[4];

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView = (msaaEnabled ? m_msaaDepthRenderTarget : m_depthRenderTarget)->vkImageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    // When using a depth prepass the buffer is already filled — load it.
    // Opaque objects use EQUAL test + no write, so STORE is still needed
    // (alpha-masked/transparent objects still write depth with LESS).
    depthAttachment.loadOp = m_hasExternalDepth ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue = m_clearValues[5];
    if (msaaEnabled)
    {
        depthAttachment.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
        depthAttachment.resolveImageView = m_depthRenderTarget->vkImageView();
        depthAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    execution.colorsRenderingItems = {normalColor, albedoColor, materialColor, emissiveColor};
    if (m_enableObjectId && objectAttachmentTarget)
        execution.colorsRenderingItems.push_back(objectColor);
    execution.depthRenderingItem = depthAttachment;

    execution.targets[m_normalTextureHandlers[renderContext.currentImageIndex]] = m_normalRenderTargets[renderContext.currentImageIndex];
    execution.targets[m_albedoTextureHandlers[renderContext.currentImageIndex]] = m_albedoRenderTargets[renderContext.currentImageIndex];
    execution.targets[m_materialTextureHandlers[renderContext.currentImageIndex]] = m_materialRenderTargets[renderContext.currentImageIndex];
    execution.targets[m_emissiveTextureHandlers[renderContext.currentImageIndex]] = m_emissiveRenderTargets[renderContext.currentImageIndex];
    if (msaaEnabled)
    {
        execution.targets[m_msaaNormalTextureHandlers[renderContext.currentImageIndex]] = m_msaaNormalRenderTargets[renderContext.currentImageIndex];
        execution.targets[m_msaaAlbedoTextureHandlers[renderContext.currentImageIndex]] = m_msaaAlbedoRenderTargets[renderContext.currentImageIndex];
        execution.targets[m_msaaMaterialTextureHandlers[renderContext.currentImageIndex]] = m_msaaMaterialRenderTargets[renderContext.currentImageIndex];
        execution.targets[m_msaaEmissiveTextureHandlers[renderContext.currentImageIndex]] = m_msaaEmissiveRenderTargets[renderContext.currentImageIndex];
        execution.targets[m_msaaDepthTextureHandler] = m_msaaDepthRenderTarget;
    }

    if (!msaaEnabled && m_enableObjectId && m_objectIdRenderTarget && m_objectIdTextureHandler.isValid())
        execution.targets[m_objectIdTextureHandler] = m_objectIdRenderTarget;
    if (msaaEnabled && m_enableObjectId && m_msaaObjectIdRenderTarget && m_msaaObjectIdTextureHandler.isValid())
        execution.targets[m_msaaObjectIdTextureHandler] = m_msaaObjectIdRenderTarget;
    execution.targets[m_depthTextureHandler] = m_depthRenderTarget;

    return {execution};
}

void GBufferRenderGraphPass::setExtent(VkExtent2D extent)
{
    if (m_extent.width == extent.width && m_extent.height == extent.height)
        return;

    m_extent = extent;
    m_viewport = VkViewport{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    m_scissor = VkRect2D{VkOffset2D{0, 0}, m_extent};
    requestRecompilation();
}

void GBufferRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    m_depthRenderTarget = storage.getTexture(m_depthTextureHandler);
    m_msaaDepthRenderTarget = m_msaaDepthTextureHandler.isValid() ? storage.getTexture(m_msaaDepthTextureHandler) : nullptr;
    m_objectIdRenderTarget = (m_enableObjectId && m_objectIdTextureHandler.isValid()) ? storage.getTexture(m_objectIdTextureHandler) : nullptr;
    m_msaaObjectIdRenderTarget = (m_enableObjectId && m_msaaObjectIdTextureHandler.isValid()) ? storage.getTexture(m_msaaObjectIdTextureHandler) : nullptr;

    const int imageCount = static_cast<int>(core::VulkanContext::getContext()->getSwapchain()->getImages().size());
    m_normalRenderTargets.resize(imageCount);
    m_msaaNormalRenderTargets.resize(imageCount);
    m_albedoRenderTargets.resize(imageCount);
    m_msaaAlbedoRenderTargets.resize(imageCount);
    m_materialRenderTargets.resize(imageCount);
    m_msaaMaterialRenderTargets.resize(imageCount);
    m_emissiveRenderTargets.resize(imageCount);
    m_msaaEmissiveRenderTargets.resize(imageCount);

    for (int imageIndex = 0; imageIndex < imageCount; ++imageIndex)
    {
        m_normalRenderTargets[imageIndex] = storage.getTexture(m_normalTextureHandlers[imageIndex]);
        m_msaaNormalRenderTargets[imageIndex] = m_msaaNormalTextureHandlers.empty() ? nullptr : storage.getTexture(m_msaaNormalTextureHandlers[imageIndex]);
        m_albedoRenderTargets[imageIndex] = storage.getTexture(m_albedoTextureHandlers[imageIndex]);
        m_msaaAlbedoRenderTargets[imageIndex] = m_msaaAlbedoTextureHandlers.empty() ? nullptr : storage.getTexture(m_msaaAlbedoTextureHandlers[imageIndex]);
        m_materialRenderTargets[imageIndex] = storage.getTexture(m_materialTextureHandlers[imageIndex]);
        m_msaaMaterialRenderTargets[imageIndex] = m_msaaMaterialTextureHandlers.empty() ? nullptr : storage.getTexture(m_msaaMaterialTextureHandlers[imageIndex]);
        m_emissiveRenderTargets[imageIndex] = storage.getTexture(m_emissiveTextureHandlers[imageIndex]);
        m_msaaEmissiveRenderTargets[imageIndex] = m_msaaEmissiveTextureHandlers.empty() ? nullptr : storage.getTexture(m_msaaEmissiveTextureHandlers[imageIndex]);
    }
}

void GBufferRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_normalTextureHandlers.clear();
    m_msaaNormalTextureHandlers.clear();
    m_albedoTextureHandlers.clear();
    m_msaaAlbedoTextureHandlers.clear();
    m_materialTextureHandlers.clear();
    m_msaaMaterialTextureHandlers.clear();
    m_emissiveTextureHandlers.clear();
    m_msaaEmissiveTextureHandlers.clear();
    m_colorFormats.clear();
    m_msaaDepthTextureHandler = {};
    m_objectIdTextureHandler = {};
    m_msaaObjectIdTextureHandler = {};
    m_rasterizationSamples = resolveGBufferSampleCount();

    bool msaaEnabled = m_rasterizationSamples != VK_SAMPLE_COUNT_1_BIT;
    if (msaaEnabled && m_hasExternalDepth &&
        (!m_externalDepthHandlerPtr || !m_externalResolvedDepthHandlerPtr || !m_externalResolvedDepthHandlerPtr->isValid()))
    {
        VX_ENGINE_WARNING_STREAM("GBuffer MSAA requested, but the external depth-prepass path does not expose both multisampled and resolved depth targets. Falling back to Off.\n");
        m_rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        msaaEnabled = false;
    }

    const VkFormat normalFormat = VK_FORMAT_R8G8B8A8_UNORM;
    const VkFormat albedoFormat = VK_FORMAT_R8G8B8A8_UNORM;
    const VkFormat materialFormat = VK_FORMAT_R8G8B8A8_UNORM;
    const VkFormat emissiveFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    m_colorFormats = {normalFormat, albedoFormat, materialFormat, emissiveFormat};
    if (m_enableObjectId)
        m_colorFormats.push_back(VK_FORMAT_R32_UINT);
    m_depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());

    RGPTextureDescription normalTextureDescription{normalFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription albedoTextureDescription{albedoFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription materialTextureDescription{materialFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription emissiveTextureDescription{emissiveFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription objectIdTextureDescription{VK_FORMAT_R32_UINT, RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription depthTextureDescription{m_depthFormat, RGPTextureUsage::DEPTH_STENCIL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    RGPTextureDescription msaaNormalTextureDescription{normalFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    RGPTextureDescription msaaAlbedoTextureDescription{albedoFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    RGPTextureDescription msaaMaterialTextureDescription{materialFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    RGPTextureDescription msaaEmissiveTextureDescription{emissiveFormat, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    RGPTextureDescription msaaObjectIdTextureDescription{VK_FORMAT_R32_UINT, RGPTextureUsage::COLOR_ATTACHMENT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    RGPTextureDescription msaaDepthTextureDescription{m_depthFormat, RGPTextureUsage::DEPTH_STENCIL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    normalTextureDescription.setCustomExtentFunction([this]
                                                     { return m_extent; });
    albedoTextureDescription.setCustomExtentFunction([this]
                                                     { return m_extent; });
    materialTextureDescription.setCustomExtentFunction([this]
                                                       { return m_extent; });
    emissiveTextureDescription.setCustomExtentFunction([this]
                                                       { return m_extent; });
    objectIdTextureDescription.setCustomExtentFunction([this]
                                                       { return m_extent; });
    depthTextureDescription.setCustomExtentFunction([this]
                                                    { return m_extent; });
    msaaNormalTextureDescription.setCustomExtentFunction([this]
                                                         { return m_extent; });
    msaaAlbedoTextureDescription.setCustomExtentFunction([this]
                                                         { return m_extent; });
    msaaMaterialTextureDescription.setCustomExtentFunction([this]
                                                           { return m_extent; });
    msaaEmissiveTextureDescription.setCustomExtentFunction([this]
                                                           { return m_extent; });
    msaaObjectIdTextureDescription.setCustomExtentFunction([this]
                                                           { return m_extent; });
    msaaDepthTextureDescription.setCustomExtentFunction([this]
                                                        { return m_extent; });

    msaaNormalTextureDescription.setSampleCount(m_rasterizationSamples);
    msaaAlbedoTextureDescription.setSampleCount(m_rasterizationSamples);
    msaaMaterialTextureDescription.setSampleCount(m_rasterizationSamples);
    msaaEmissiveTextureDescription.setSampleCount(m_rasterizationSamples);
    msaaObjectIdTextureDescription.setSampleCount(m_rasterizationSamples);
    msaaDepthTextureDescription.setSampleCount(m_rasterizationSamples);

    objectIdTextureDescription.setDebugName("__ELIX_OBJECT_ID_GBUFFER_TEXTURE__");
    depthTextureDescription.setDebugName("__ELIX_DEPTH_GBUFFER_TEXTURE__");

    if (m_enableObjectId)
    {
        m_objectIdTextureHandler = builder.createTexture(objectIdTextureDescription);
        if (msaaEnabled)
        {
            msaaObjectIdTextureDescription.setDebugName("__ELIX_OBJECT_ID_GBUFFER_MSAA_TEXTURE__");
            m_msaaObjectIdTextureHandler = builder.createTexture(msaaObjectIdTextureDescription);
            builder.write(m_msaaObjectIdTextureHandler, RGPTextureUsage::COLOR_ATTACHMENT);
        }
        else
        {
            builder.write(m_objectIdTextureHandler, RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC);
        }
    }

    if (m_hasExternalDepth && m_externalDepthHandlerPtr)
    {
        if (msaaEnabled)
        {
            m_msaaDepthTextureHandler = *m_externalDepthHandlerPtr;
            m_depthTextureHandler = *m_externalResolvedDepthHandlerPtr;
            builder.read(m_msaaDepthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
            builder.write(m_msaaDepthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
            builder.write(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
        }
        else
        {
            // Dereference the live pointer — by the time GBuffer's setup() runs,
            // the depth prepass (added earlier, lower id) has already assigned its
            // handler ID via builder.createTexture().
            m_depthTextureHandler = *m_externalDepthHandlerPtr;
            builder.read(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
            builder.write(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
        }
    }
    else
    {
        if (msaaEnabled)
        {
            msaaDepthTextureDescription.setDebugName("__ELIX_DEPTH_GBUFFER_MSAA_TEXTURE__");
            m_msaaDepthTextureHandler = builder.createTexture(msaaDepthTextureDescription);
            m_depthTextureHandler = builder.createTexture(depthTextureDescription);
            builder.write(m_msaaDepthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
            builder.write(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
        }
        else
        {
            m_depthTextureHandler = builder.createTexture(depthTextureDescription);
            builder.write(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
        }
    }

    const int imageCount = static_cast<int>(core::VulkanContext::getContext()->getSwapchain()->getImages().size());
    m_normalTextureHandlers.reserve(imageCount);
    m_msaaNormalTextureHandlers.reserve(imageCount);
    m_albedoTextureHandlers.reserve(imageCount);
    m_msaaAlbedoTextureHandlers.reserve(imageCount);
    m_materialTextureHandlers.reserve(imageCount);
    m_msaaMaterialTextureHandlers.reserve(imageCount);
    m_emissiveTextureHandlers.reserve(imageCount);
    m_msaaEmissiveTextureHandlers.reserve(imageCount);

    for (int imageIndex = 0; imageIndex < imageCount; ++imageIndex)
    {
        normalTextureDescription.setDebugName("__ELIX_NORMAL_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");
        albedoTextureDescription.setDebugName("__ELIX_ALBEDO_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");
        materialTextureDescription.setDebugName("__ELIX_MATERIAL_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");
        emissiveTextureDescription.setDebugName("__ELIX_EMISSIVE_GBUFFER_TEXTURE_" + std::to_string(imageIndex) + "__");

        const auto normalTexture = builder.createTexture(normalTextureDescription);
        const auto albedoTexture = builder.createTexture(albedoTextureDescription);
        const auto materialTexture = builder.createTexture(materialTextureDescription);
        const auto emissiveTexture = builder.createTexture(emissiveTextureDescription);

        m_normalTextureHandlers.push_back(normalTexture);
        m_albedoTextureHandlers.push_back(albedoTexture);
        m_materialTextureHandlers.push_back(materialTexture);
        m_emissiveTextureHandlers.push_back(emissiveTexture);

        builder.write(normalTexture, RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(albedoTexture, RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(materialTexture, RGPTextureUsage::COLOR_ATTACHMENT);
        builder.write(emissiveTexture, RGPTextureUsage::COLOR_ATTACHMENT);

        if (msaaEnabled)
        {
            msaaNormalTextureDescription.setDebugName("__ELIX_NORMAL_GBUFFER_MSAA_TEXTURE_" + std::to_string(imageIndex) + "__");
            msaaAlbedoTextureDescription.setDebugName("__ELIX_ALBEDO_GBUFFER_MSAA_TEXTURE_" + std::to_string(imageIndex) + "__");
            msaaMaterialTextureDescription.setDebugName("__ELIX_MATERIAL_GBUFFER_MSAA_TEXTURE_" + std::to_string(imageIndex) + "__");
            msaaEmissiveTextureDescription.setDebugName("__ELIX_EMISSIVE_GBUFFER_MSAA_TEXTURE_" + std::to_string(imageIndex) + "__");

            const auto msaaNormalTexture = builder.createTexture(msaaNormalTextureDescription);
            const auto msaaAlbedoTexture = builder.createTexture(msaaAlbedoTextureDescription);
            const auto msaaMaterialTexture = builder.createTexture(msaaMaterialTextureDescription);
            const auto msaaEmissiveTexture = builder.createTexture(msaaEmissiveTextureDescription);

            m_msaaNormalTextureHandlers.push_back(msaaNormalTexture);
            m_msaaAlbedoTextureHandlers.push_back(msaaAlbedoTexture);
            m_msaaMaterialTextureHandlers.push_back(msaaMaterialTexture);
            m_msaaEmissiveTextureHandlers.push_back(msaaEmissiveTexture);

            builder.write(msaaNormalTexture, RGPTextureUsage::COLOR_ATTACHMENT);
            builder.write(msaaAlbedoTexture, RGPTextureUsage::COLOR_ATTACHMENT);
            builder.write(msaaMaterialTexture, RGPTextureUsage::COLOR_ATTACHMENT);
            builder.write(msaaEmissiveTexture, RGPTextureUsage::COLOR_ATTACHMENT);
        }
    }

    outputs.normals.set(m_normalTextureHandlers);
    outputs.albedo.set(m_albedoTextureHandlers);
    outputs.material.set(m_materialTextureHandlers);
    outputs.emissive.set(m_emissiveTextureHandlers);
    outputs.depth.set(m_depthTextureHandler);
    outputs.objectId.set(m_objectIdTextureHandler);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
