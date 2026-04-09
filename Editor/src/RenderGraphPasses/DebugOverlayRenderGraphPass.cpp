#include "Editor/RenderGraphPasses/DebugOverlayRenderGraphPass.hpp"

#include "Engine/DebugDraw.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/Buffer.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

DebugOverlayRenderGraphPass::DebugOverlayRenderGraphPass(
    std::vector<engine::renderGraph::RGPResourceHandler> &inputColorHandlers)
    : m_inputHandlers(inputColorHandlers)
{
    setDebugName("Debug overlay render graph pass");
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void DebugOverlayRenderGraphPass::setup(engine::renderGraph::RGPResourcesBuilder &builder)
{
    m_format = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();

    // Read input color as sampled (used for the blit)
    for (const auto &h : m_inputHandlers)
        builder.read(h, engine::renderGraph::RGPTextureUsage::SAMPLED);

    // Create output textures (same format, one per swapchain image)
    engine::renderGraph::RGPTextureDescription desc{m_format, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT};
    desc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    desc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    desc.setCustomExtentFunction([this] { return m_extent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputHandlers.clear();
    m_outputHandlers.reserve(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        desc.setDebugName("__ELIX_DEBUG_OVERLAY_" + std::to_string(i) + "__");
        auto h = builder.createTexture(desc);
        m_outputHandlers.push_back(h);
        builder.write(h, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
    }

    auto device = core::VulkanContext::getContext()->getDevice();

    // ── Blit pipeline layout ─────────────────────────────────────────────────
    VkDescriptorSetLayoutBinding blitBinding{};
    blitBinding.binding         = 0;
    blitBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    blitBinding.descriptorCount = 1;
    blitBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_blitDescriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{blitBinding});

    m_blitPipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_blitDescriptorSetLayout},
        std::vector<VkPushConstantRange>{});

    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR,
                                            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                            VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    // ── Overlay pipeline layout ──────────────────────────────────────────────
    // set 0 = camera UBO (shared cameraDescriptorSetLayout)
    m_overlayPipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
            *engine::EngineShaderFamilies::cameraDescriptorSetLayout},
        std::vector<VkPushConstantRange>{});
}

void DebugOverlayRenderGraphPass::compile(engine::renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_colorRenderTargets.resize(imageCount);
    m_blitDescriptorSets.resize(imageCount);

    auto device = core::VulkanContext::getContext()->getDevice();
    auto pool   = core::VulkanContext::getContext()->getPersistentDescriptorPool();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_colorRenderTargets[i] = storage.getTexture(m_outputHandlers[i]);
        auto inputTex = storage.getTexture(m_inputHandlers[i]);

        if (!m_descriptorSetsBuilt || m_blitDescriptorSets[i] == VK_NULL_HANDLE)
        {
            m_blitDescriptorSets[i] = engine::DescriptorSetBuilder::begin()
                .addImage(inputTex->vkImageView(), m_sampler,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .build(device, pool, m_blitDescriptorSetLayout);
        }
        else
        {
            engine::DescriptorSetBuilder::begin()
                .addImage(inputTex->vkImageView(), m_sampler,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .update(device, m_blitDescriptorSets[i]);
        }
    }
    m_descriptorSetsBuilt = true;

    // Per-frame host-visible vertex buffers for debug geometry
    if (m_lineVertexBuffers.empty())
    {
        m_lineVertexBuffers.resize(imageCount);
        const VkDeviceSize bufSize = static_cast<VkDeviceSize>(kMaxDebugLineVertices) *
                                     sizeof(engine::DebugDraw::Vertex);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            m_lineVertexBuffers[i] = core::Buffer::createShared(
                bufSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                core::memory::MemoryUsage::CPU_TO_GPU);
        }
    }

    if (m_triangleVertexBuffers.empty())
    {
        m_triangleVertexBuffers.resize(imageCount);
        const VkDeviceSize bufSize = static_cast<VkDeviceSize>(kMaxDebugTriangleVertices) *
                                     sizeof(engine::DebugDraw::Vertex);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            m_triangleVertexBuffers[i] = core::Buffer::createShared(
                bufSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                core::memory::MemoryUsage::CPU_TO_GPU);
        }
    }
}

void DebugOverlayRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                         const engine::RenderGraphPassPerFrameData &data,
                                         const engine::RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    // ── 1. Blit: copy input to output with a fullscreen triangle ─────────────
    engine::GraphicsPipelineKey blitKey{};
    blitKey.shader        = engine::ShaderId::DebugBlit;
    blitKey.cull          = engine::CullMode::None;
    blitKey.depthTest     = false;
    blitKey.depthWrite    = false;
    blitKey.depthCompare  = VK_COMPARE_OP_LESS;
    blitKey.polygonMode   = VK_POLYGON_MODE_FILL;
    blitKey.topology      = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    blitKey.colorFormats  = {m_format};
    blitKey.pipelineLayout = m_blitPipelineLayout;

    auto blitPipeline = engine::GraphicsPipelineManager::getOrCreate(blitKey);
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeline);
    vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_blitPipelineLayout, 0, 1,
                            &m_blitDescriptorSets[renderContext.currentImageIndex], 0, nullptr);
    engine::renderGraph::profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);

    // ── 2. Debug triangles and lines ─────────────────────────────────────────
    static thread_local std::vector<engine::DebugDraw::Vertex> lineVertices;
    static thread_local std::vector<engine::DebugDraw::Vertex> triangleVertices;
    lineVertices.clear();
    triangleVertices.clear();
    engine::DebugDraw::collectVertices(lineVertices, triangleVertices);

    if (lineVertices.empty() && triangleVertices.empty())
        return;

    vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_overlayPipelineLayout, 0, 1,
                            &data.cameraDescriptorSet, 0, nullptr);

    const uint32_t triangleDrawCount = static_cast<uint32_t>(
        std::min(triangleVertices.size(), static_cast<size_t>(kMaxDebugTriangleVertices)));
    const uint32_t triangleVertexCount = triangleDrawCount - (triangleDrawCount % 3u);
    if (triangleVertexCount > 0u)
    {
        auto &vb = m_triangleVertexBuffers[renderContext.currentImageIndex];
        void *mapped = nullptr;
        vb->map(mapped);
        std::memcpy(mapped, triangleVertices.data(), triangleVertexCount * sizeof(engine::DebugDraw::Vertex));
        vb->unmap();

        engine::GraphicsPipelineKey triangleKey{};
        triangleKey.shader = engine::ShaderId::DebugLines;
        triangleKey.blend = engine::BlendMode::AlphaBlend;
        triangleKey.cull = engine::CullMode::None;
        triangleKey.depthTest = false;
        triangleKey.depthWrite = false;
        triangleKey.depthCompare = VK_COMPARE_OP_LESS;
        triangleKey.polygonMode = VK_POLYGON_MODE_FILL;
        triangleKey.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        triangleKey.colorFormats = {m_format};
        triangleKey.pipelineLayout = m_overlayPipelineLayout;

        auto trianglePipeline = engine::GraphicsPipelineManager::getOrCreate(triangleKey);
        vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);

        const VkDeviceSize offset = 0;
        VkBuffer rawVB = vb->vk();
        vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, &rawVB, &offset);
        engine::renderGraph::profiling::cmdDraw(commandBuffer, triangleVertexCount, 1, 0, 0);
    }

    const uint32_t lineDrawCount = static_cast<uint32_t>(
        std::min(lineVertices.size(), static_cast<size_t>(kMaxDebugLineVertices)));
    const uint32_t lineVertexCount = lineDrawCount & ~1u;
    if (lineVertexCount > 0u)
    {
        auto &vb = m_lineVertexBuffers[renderContext.currentImageIndex];
        void *mapped = nullptr;
        vb->map(mapped);
        std::memcpy(mapped, lineVertices.data(), lineVertexCount * sizeof(engine::DebugDraw::Vertex));
        vb->unmap();

        engine::GraphicsPipelineKey lineKey{};
        lineKey.shader = engine::ShaderId::DebugLines;
        lineKey.blend = engine::BlendMode::AlphaBlend;
        lineKey.cull = engine::CullMode::None;
        lineKey.depthTest = false;
        lineKey.depthWrite = false;
        lineKey.depthCompare = VK_COMPARE_OP_LESS;
        lineKey.polygonMode = VK_POLYGON_MODE_FILL;
        lineKey.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        lineKey.colorFormats = {m_format};
        lineKey.pipelineLayout = m_overlayPipelineLayout;

        auto linesPipeline = engine::GraphicsPipelineManager::getOrCreate(lineKey);
        vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, linesPipeline);

        const VkDeviceSize offset = 0;
        VkBuffer rawVB = vb->vk();
        vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, &rawVB, &offset);
        engine::renderGraph::profiling::cmdDraw(commandBuffer, lineVertexCount, 1, 0, 0);
    }
}

std::vector<engine::renderGraph::IRenderGraphPass::RenderPassExecution>
DebugOverlayRenderGraphPass::getRenderPassExecutions(
    const engine::RenderGraphPassContext &renderContext) const
{
    engine::renderGraph::IRenderGraphPass::RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView   = m_colorRenderTargets[renderContext.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue  = {.color = {0.0f, 0.0f, 0.0f, 1.0f}};

    exec.colorsRenderingItems = {color};
    exec.useDepth    = false;
    exec.colorFormats = {m_format};
    exec.depthFormat  = VK_FORMAT_UNDEFINED;
    exec.targets[m_outputHandlers[renderContext.currentImageIndex]] =
        m_colorRenderTargets[renderContext.currentImageIndex];

    return {exec};
}

void DebugOverlayRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent   = extent;
    m_viewport = {0.0f, 0.0f, float(extent.width), float(extent.height), 0.0f, 1.0f};
    m_scissor  = {{0, 0}, extent};
    requestRecompilation();
}

void DebugOverlayRenderGraphPass::freeResources()
{
    m_colorRenderTargets.clear();
    for (auto &s : m_blitDescriptorSets)
        s = VK_NULL_HANDLE;
    m_descriptorSetsBuilt = false;
    m_lineVertexBuffers.clear();
    m_triangleVertexBuffers.clear();
}

ELIX_NESTED_NAMESPACE_END
