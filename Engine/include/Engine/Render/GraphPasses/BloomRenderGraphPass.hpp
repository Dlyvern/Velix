#ifndef ELIX_BLOOM_RENDER_GRAPH_PASS_HPP
#define ELIX_BLOOM_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/RenderTarget.hpp"
#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

/// Reads HDR scene color (from SkyLight), extracts bright regions via a 13-tap
/// filter and writes them to a half-resolution bloom texture.
class BloomRenderGraphPass : public IRenderGraphPass
{
public:
    explicit BloomRenderGraphPass(std::vector<RGPResourceHandler> &hdrInputHandlers);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    std::vector<RGPResourceHandler> &getHandlers() { return m_bloomHandlers; }

private:
    std::array<VkClearValue, 1> m_clearValues;

    std::vector<const RenderTarget *> m_bloomRenderTargets;

    VkFormat m_bloomFormat{VK_FORMAT_R16G16B16A16_SFLOAT};

    std::vector<RGPResourceHandler> &m_hdrInputHandlers;
    std::vector<RGPResourceHandler>  m_bloomHandlers;

    core::PipelineLayout::SharedPtr      m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_descriptorSets;
    bool                         m_descriptorSetsInitialized{false};

    VkExtent2D m_extent;        // full-res extent (used to compute texel size)
    VkExtent2D m_bloomExtent;   // half-res extent
    VkViewport m_viewport;
    VkRect2D   m_scissor;

    core::Sampler::SharedPtr m_sampler{nullptr};
};

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END

#endif // ELIX_BLOOM_RENDER_GRAPH_PASS_HPP
