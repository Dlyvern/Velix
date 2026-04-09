#ifndef ELIX_DEBUG_OVERLAY_RENDER_GRAPH_PASS_HPP
#define ELIX_DEBUG_OVERLAY_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/DescriptorSetLayout.hpp"
#include "Core/Sampler.hpp"
#include "Core/Buffer.hpp"

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

/// Renders DebugDraw lines on top of the scene.
/// Inserted after SelectionOverlay, before UI.
/// Editor-only — never created in game builds.
class DebugOverlayRenderGraphPass : public engine::renderGraph::IRenderGraphPass
{
public:
    explicit DebugOverlayRenderGraphPass(
        std::vector<engine::renderGraph::RGPResourceHandler> &inputColorHandlers);

    void setup(engine::renderGraph::RGPResourcesBuilder &builder) override;
    void compile(engine::renderGraph::RGPResourcesStorage &storage) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer,
                const engine::RenderGraphPassPerFrameData &data,
                const engine::RenderGraphPassContext &renderContext) override;

    std::vector<engine::renderGraph::IRenderGraphPass::RenderPassExecution>
        getRenderPassExecutions(const engine::RenderGraphPassContext &renderContext) const override;

    std::vector<engine::renderGraph::RGPResourceHandler> &getHandlers() { return m_outputHandlers; }

    void setExtent(VkExtent2D extent);
    void freeResources() override;

private:
    static constexpr uint32_t kMaxDebugLineVertices = 131072;
    static constexpr uint32_t kMaxDebugTriangleVertices = 196608;

    std::vector<engine::renderGraph::RGPResourceHandler> &m_inputHandlers;
    std::vector<engine::renderGraph::RGPResourceHandler>  m_outputHandlers;
    std::vector<const engine::RenderTarget *>             m_colorRenderTargets;

    VkFormat   m_format{VK_FORMAT_UNDEFINED};
    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D   m_scissor{};

    // Blit pipeline resources (pass-through input → output)
    core::DescriptorSetLayout::SharedPtr m_blitDescriptorSetLayout;
    core::PipelineLayout::SharedPtr      m_blitPipelineLayout;
    std::vector<VkDescriptorSet>         m_blitDescriptorSets;
    core::Sampler::SharedPtr             m_sampler;
    bool m_descriptorSetsBuilt{false};

    // Overlay pipeline resources (camera set 0, no additional descriptors)
    core::PipelineLayout::SharedPtr m_overlayPipelineLayout;

    // Per-frame host-visible vertex buffers for debug geometry
    std::vector<core::Buffer::SharedPtr> m_lineVertexBuffers;
    std::vector<core::Buffer::SharedPtr> m_triangleVertexBuffers;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_DEBUG_OVERLAY_RENDER_GRAPH_PASS_HPP
