#ifndef ELIX_LIGHTING_RENDER_GRAPH_PASS_HPP
#define ELIX_LIGHTING_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class LightingRenderGraphPass : public IRenderGraphPass
{
public:
    LightingRenderGraphPass(RGPResourceHandler &shadowTextureHandler,
                            RGPResourceHandler &depthTextureHandler,
                            std::vector<RGPResourceHandler> &albedoTextureHandlers,
                            std::vector<RGPResourceHandler> &normalTextureHandlers,
                            std::vector<RGPResourceHandler> &materialTextureHandlers);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    std::vector<RGPResourceHandler> &getOutput()
    {
        return m_colorTextureHandler;
    }

private:
    std::array<VkClearValue, 1> m_clearValues;

    std::vector<const RenderTarget *> m_colorRenderTargets;

    VkFormat m_colorFormat;

    VkExtent2D m_extent;
    VkViewport m_viewport;
    VkRect2D m_scissor;

    std::vector<RGPResourceHandler> m_colorTextureHandler;

    RGPResourceHandler &m_shadowTextureHandler;
    RGPResourceHandler &m_depthTextureHandler;
    std::vector<RGPResourceHandler> &m_albedoTextureHandlers;
    std::vector<RGPResourceHandler> &m_normalTextureHandlers;
    std::vector<RGPResourceHandler> &m_materialTextureHandlers;

    core::Sampler::SharedPtr m_defaultSampler{nullptr};
    core::Sampler::SharedPtr m_sampler{nullptr};

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_descriptorSets{VK_NULL_HANDLE};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_LIGHTING_RENDER_GRAPH_PASS_HPP
