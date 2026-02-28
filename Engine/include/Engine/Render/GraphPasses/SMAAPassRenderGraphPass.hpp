#ifndef ELIX_SMAA_PASS_RENDER_GRAPH_PASS_HPP
#define ELIX_SMAA_PASS_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/RenderTarget.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

/// Subpixel Morphological Anti-Aliasing (SMAA) pass.
/// Single-pass implementation: performs edge-aware blending in the fragment shader.
/// When disabled (RenderQualitySettings::enableSMAA == false), passes through unmodified.
class SMAAPassRenderGraphPass : public IRenderGraphPass
{
public:
    explicit SMAAPassRenderGraphPass(std::vector<RGPResourceHandler> &inputHandlers);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    std::vector<RGPResourceHandler> &getHandlers() { return m_outputHandlers; }

private:
    std::vector<RGPResourceHandler> &m_inputHandlers;
    std::vector<RGPResourceHandler>  m_outputHandlers;

    std::vector<const RenderTarget *> m_outputTargets;

    VkFormat m_format{VK_FORMAT_UNDEFINED};

    core::PipelineLayout::SharedPtr      m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_sampler{nullptr};

    VkExtent2D m_extent;
    VkViewport m_viewport;
    VkRect2D   m_scissor;

    std::array<VkClearValue, 1> m_clearValues;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SMAA_PASS_RENDER_GRAPH_PASS_HPP
