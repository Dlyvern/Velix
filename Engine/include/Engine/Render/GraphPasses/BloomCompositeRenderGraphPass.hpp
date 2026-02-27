#ifndef ELIX_BLOOM_COMPOSITE_RENDER_GRAPH_PASS_HPP
#define ELIX_BLOOM_COMPOSITE_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/RenderTarget.hpp"
#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

/// Upsamples the half-res bloom texture and additively blends it onto the
/// tonemapped (LDR) scene output.
class BloomCompositeRenderGraphPass : public IRenderGraphPass
{
public:
    BloomCompositeRenderGraphPass(std::vector<RGPResourceHandler> &ldrInputHandlers,
                                  std::vector<RGPResourceHandler> &bloomHandlers);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    std::vector<RGPResourceHandler> &getHandlers() { return m_outputHandlers; }

private:
    std::array<VkClearValue, 1> m_clearValues;

    std::vector<const RenderTarget *> m_outputRenderTargets;

    VkFormat m_format;

    std::vector<RGPResourceHandler> &m_ldrInputHandlers;
    std::vector<RGPResourceHandler> &m_bloomHandlers;
    std::vector<RGPResourceHandler> m_outputHandlers;

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsInitialized{false};

    VkExtent2D m_extent;
    VkViewport m_viewport;
    VkRect2D m_scissor;

    core::Sampler::SharedPtr m_sampler{nullptr};
};

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END

#endif // ELIX_BLOOM_COMPOSITE_RENDER_GRAPH_PASS_HPP
