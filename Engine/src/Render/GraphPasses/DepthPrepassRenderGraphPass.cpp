#include "Engine/Render/GraphPasses/DepthPrepassRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Core/Logger.hpp"

#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Builders/GraphicsPipelineKey.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Material.hpp"

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

    VkSampleCountFlagBits resolveDepthPrepassSampleCount()
    {
        const auto context = core::VulkanContext::getContext();
        const auto requested = RenderQualitySettings::getInstance().getRequestedMsaaSampleCount();

        if (requested == VK_SAMPLE_COUNT_1_BIT)
            return VK_SAMPLE_COUNT_1_BIT;

        const auto clamped = context->clampSupportedSampleCount(requested);
        if (clamped != requested)
        {
            VX_ENGINE_WARNING_STREAM("DepthPrepass MSAA requested "
                                     << sampleCountToInt(requested)
                                     << "x, but the adapter only supports up to "
                                     << sampleCountToInt(clamped)
                                     << "x for color+depth framebuffers. Clamping.\n");
        }

        if (!context->supportsSampleZeroDepthResolve())
        {
            VX_ENGINE_WARNING_STREAM("DepthPrepass MSAA requested "
                                     << sampleCountToInt(requested)
                                     << "x, but depth resolve mode SAMPLE_ZERO is unavailable. Falling back to Off.\n");
            return VK_SAMPLE_COUNT_1_BIT;
        }

        return clamped;
    }

    struct DepthPrepassPC
    {
        uint32_t baseInstance{0};
        uint32_t _pad0{0};
        uint32_t _pad1{0};
        uint32_t _pad2{0};
        float    time{0.0f};
    };
}

DepthPrepassRenderGraphPass::DepthPrepassRenderGraphPass()
{
    m_depthClear.depthStencil = {1.0f, 0};
    setDebugName("Depth Prepass");
    outputs.depth.setOwner(this);
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void DepthPrepassRenderGraphPass::setExtent(VkExtent2D extent)
{
    if (m_extent.width == extent.width && m_extent.height == extent.height)
        return;

    m_extent   = extent;
    m_viewport = VkViewport{0.0f, 0.0f,
                            static_cast<float>(extent.width), static_cast<float>(extent.height),
                            0.0f, 1.0f};
    m_scissor  = VkRect2D{VkOffset2D{0, 0}, extent};
    requestRecompilation();
}

void DepthPrepassRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());
    m_rasterizationSamples = resolveDepthPrepassSampleCount();
    const bool msaaEnabled = m_rasterizationSamples != VK_SAMPLE_COUNT_1_BIT;

    RGPTextureDescription resolvedDepthDesc{m_depthFormat,
                                            RGPTextureUsage::DEPTH_STENCIL,
                                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    resolvedDepthDesc.setDebugName("__ELIX_DEPTH_PREPASS_TEXTURE__");
    resolvedDepthDesc.setCustomExtentFunction([this] { return m_extent; });

    if (msaaEnabled)
    {
        RGPTextureDescription depthAttachmentDesc{m_depthFormat,
                                                  RGPTextureUsage::DEPTH_STENCIL,
                                                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        depthAttachmentDesc.setDebugName("__ELIX_DEPTH_PREPASS_MSAA_TEXTURE__");
        depthAttachmentDesc.setCustomExtentFunction([this] { return m_extent; });
        depthAttachmentDesc.setSampleCount(m_rasterizationSamples);

        m_depthAttachmentTextureHandler = builder.createTexture(depthAttachmentDesc);
        m_depthTextureHandler = builder.createTexture(resolvedDepthDesc);

        builder.write(m_depthAttachmentTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
        builder.write(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
    }
    else
    {
        m_depthTextureHandler = builder.createTexture(resolvedDepthDesc);
        m_depthAttachmentTextureHandler = m_depthTextureHandler;
        builder.write(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
    }

    outputs.depth.set(m_depthTextureHandler);
}

void DepthPrepassRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    m_depthRenderTarget = storage.getTexture(m_depthTextureHandler);
    m_depthAttachmentRenderTarget = storage.getTexture(m_depthAttachmentTextureHandler);
}

void DepthPrepassRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                         const RenderGraphPassPerFrameData &data,
                                         const RenderGraphPassContext &)
{
    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    const VkPipelineLayout pipelineLayout =
        EngineShaderFamilies::bindlessMeshPipelineLayout
            ? EngineShaderFamilies::bindlessMeshPipelineLayout
            : static_cast<VkPipelineLayout>(EngineShaderFamilies::meshShaderFamily.pipelineLayout);

    const bool hasUnifiedGeometry =
        data.unifiedStaticVertexBuffer != VK_NULL_HANDLE &&
        data.unifiedStaticIndexBuffer  != VK_NULL_HANDLE;

    VkPipeline boundPipeline     = VK_NULL_HANDLE;
    VkBuffer   boundVertexBuffer = VK_NULL_HANDLE;
    VkBuffer   boundIndexBuffer  = VK_NULL_HANDLE;
    VkBuffer   boundUnifiedVB    = VK_NULL_HANDLE;
    VkBuffer   boundUnifiedIB    = VK_NULL_HANDLE;

    for (const auto &batch : data.drawBatches)
    {
        if (!batch.mesh || !batch.material)
            continue;

        // Skip transparent and alpha-masked objects — they can't go through a depth-only prepass
        // (alpha-masked needs fragment alpha test, alpha-blend has no opaque depth contribution).
        const uint32_t flags = batch.material->params().flags;
        const bool alphaBlend = (flags & Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND) != 0u;
        const bool alphaMask  = (flags & Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK)  != 0u;
        if (alphaBlend || alphaMask)
            continue;

        const bool twoSided = (flags & Material::MaterialFlags::EMATERIAL_FLAG_DOUBLE_SIDED) != 0u;

        GraphicsPipelineKey key{};
        key.shader        = batch.skinned ? ShaderId::DepthPrepassSkinned : ShaderId::DepthPrepassStatic;
        key.blend         = BlendMode::None;
        key.cull          = twoSided ? CullMode::None : CullMode::Back;
        key.depthTest     = true;
        key.depthWrite    = true;
        key.depthCompare  = VK_COMPARE_OP_LESS;
        key.polygonMode   = VK_POLYGON_MODE_FILL;
        key.topology      = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        key.rasterizationSamples = m_rasterizationSamples;
        key.colorFormats  = {};               // no color output
        key.depthFormat   = m_depthFormat;
        key.pipelineLayout = pipelineLayout;

        const VkPipeline pipeline = GraphicsPipelineManager::getOrCreate(key);
        if (pipeline != boundPipeline)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                    0, 1, &data.cameraDescriptorSet, 0, nullptr);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                    2, 1, &data.perObjectDescriptorSet, 0, nullptr);
            boundPipeline     = pipeline;
            boundVertexBuffer = VK_NULL_HANDLE;
            boundIndexBuffer  = VK_NULL_HANDLE;
            boundUnifiedVB    = VK_NULL_HANDLE;
            boundUnifiedIB    = VK_NULL_HANDLE;
        }

        const bool useUnified = hasUnifiedGeometry && !batch.skinned && batch.mesh->inUnifiedBuffer;

        if (useUnified)
        {
            if (data.unifiedStaticVertexBuffer != boundUnifiedVB)
            {
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, &data.unifiedStaticVertexBuffer, &offset);
                boundUnifiedVB    = data.unifiedStaticVertexBuffer;
                boundVertexBuffer = VK_NULL_HANDLE;
            }
            if (data.unifiedStaticIndexBuffer != boundUnifiedIB)
            {
                vkCmdBindIndexBuffer(commandBuffer, data.unifiedStaticIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                boundUnifiedIB   = data.unifiedStaticIndexBuffer;
                boundIndexBuffer = VK_NULL_HANDLE;
            }
        }
        else
        {
            if (boundUnifiedVB != VK_NULL_HANDLE || boundUnifiedIB != VK_NULL_HANDLE)
            {
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

        const DepthPrepassPC pc{.baseInstance = batch.firstInstance, .time = data.elapsedTime};
        vkCmdPushConstants(commandBuffer, pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(DepthPrepassPC), &pc);

        const uint32_t firstIndex   = useUnified ? batch.mesh->unifiedFirstIndex   : 0u;
        const int32_t  vertexOffset = useUnified ? batch.mesh->unifiedVertexOffset : 0;

        if (batch.instanceCount == 0)
            continue;

        profiling::cmdDrawIndexed(commandBuffer, batch.mesh->indicesCount, batch.instanceCount,
                                  firstIndex, vertexOffset, 0);
    }
}

std::vector<IRenderGraphPass::RenderPassExecution> DepthPrepassRenderGraphPass::getRenderPassExecutions(
    const RenderGraphPassContext &) const
{
    const bool msaaEnabled = m_rasterizationSamples != VK_SAMPLE_COUNT_1_BIT;

    IRenderGraphPass::RenderPassExecution execution{};
    execution.renderArea.offset = {0, 0};
    execution.renderArea.extent = m_extent;
    execution.colorFormats      = {};
    execution.depthFormat       = m_depthFormat;
    execution.rasterizationSamples = m_rasterizationSamples;

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView   = (msaaEnabled ? m_depthAttachmentRenderTarget : m_depthRenderTarget)->vkImageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue  = m_depthClear;

    if (msaaEnabled)
    {
        depthAttachment.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
        depthAttachment.resolveImageView = m_depthRenderTarget->vkImageView();
        depthAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    execution.depthRenderingItem = depthAttachment;
    execution.targets[m_depthTextureHandler] = m_depthRenderTarget;
    if (msaaEnabled)
        execution.targets[m_depthAttachmentTextureHandler] = m_depthAttachmentRenderTarget;

    return {execution};
}

void DepthPrepassRenderGraphPass::cleanup()
{
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
