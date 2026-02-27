#ifndef ELIX_TONEMAP_RENDER_GRAPH_PASS_HPP
#define ELIX_TONEMAP_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/RenderTarget.hpp"
#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Core/Buffer.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class TonemapRenderGraphPass : public IRenderGraphPass
{
public:
    TonemapRenderGraphPass(std::vector<RGPResourceHandler> &hdrInputHandlers);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    std::vector<RGPResourceHandler> &getHandlers()
    {
        return m_colorTextureHandler;
    }

private:
    std::array<VkClearValue, 1> m_clearValues;

    std::vector<const RenderTarget *> m_colorRenderTargets;

    VkFormat m_ldrFormat;

    std::vector<RGPResourceHandler> &m_hdrInputHandlers;
    std::vector<RGPResourceHandler> m_colorTextureHandler;

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_descriptorSets{VK_NULL_HANDLE};
    bool m_descriptorSetsInitialized{false};

    VkExtent2D m_extent;
    VkViewport m_viewport;
    VkRect2D m_scissor;

    core::Sampler::SharedPtr m_defaultSampler{nullptr};
};

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END

#endif // ELIX_TONEMAP_RENDER_GRAPH_PASS_HPP
