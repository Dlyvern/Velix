#ifndef ELIX_SSR_RENDER_GRAPH_PASS_HPP
#define ELIX_SSR_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/RenderTarget.hpp"
#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class SSRRenderGraphPass : public IRenderGraphPass
{
public:
    SSRRenderGraphPass(std::vector<RGPResourceHandler> &hdrInputHandlers,
                       std::vector<RGPResourceHandler> &normalHandlers,
                       std::vector<RGPResourceHandler> &materialHandlers,
                       RGPResourceHandler &depthHandler);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    std::vector<RGPResourceHandler> &getOutput() { return m_outputHandlers; }

private:
    std::array<VkClearValue, 1> m_clearValues;

    std::vector<const RenderTarget *> m_outputRenderTargets;

    VkFormat m_colorFormat{VK_FORMAT_R16G16B16A16_SFLOAT};

    std::vector<RGPResourceHandler> &m_hdrInputHandlers;
    std::vector<RGPResourceHandler> &m_normalHandlers;
    std::vector<RGPResourceHandler> &m_materialHandlers;
    RGPResourceHandler &m_depthHandler;
    std::vector<RGPResourceHandler> m_outputHandlers;

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_textureSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsInitialized{false};

    VkExtent2D m_extent;
    VkViewport m_viewport;
    VkRect2D m_scissor;

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};
};

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END

#endif // ELIX_SSR_RENDER_GRAPH_PASS_HPP
