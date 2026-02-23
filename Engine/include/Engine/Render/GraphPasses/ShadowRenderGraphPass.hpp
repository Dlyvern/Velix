#ifndef ELIX_SHADOW_RENDER_GRAPH_PASS_HPP
#define ELIX_SHADOW_RENDER_GRAPH_PASS_HPP

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include <cstdint>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class ShadowRenderGraphPass : public IRenderGraphPass
{
public:
    ShadowRenderGraphPass();
    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;
    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setup(renderGraph::RGPResourcesBuilder &builder) override;
    void compile(renderGraph::RGPResourcesStorage &storage) override;

    VkSampler getSampler()
    {
        return m_sampler;
    }

    VkImageView getImageView()
    {
        return m_renderTarget->vkImageView();
    }

    RGPResourceHandler &getShadowHandler()
    {
        return m_depthTextureHandler;
    }

private:
    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::Sampler::SharedPtr m_sampler{nullptr};

    const RenderTarget *m_renderTarget{nullptr};
    VkFormat m_depthFormat;

    VkViewport m_viewport{};
    VkRect2D m_scissor{};

    VkExtent2D m_extent{1024, 1024};

    VkClearValue m_clearValue;

    RGPResourceHandler m_depthTextureHandler;
};
ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SHADOW_RENDER_GRAPH_PASS_HPP