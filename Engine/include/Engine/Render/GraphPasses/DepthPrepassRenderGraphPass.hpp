#ifndef ELIX_DEPTH_PREPASS_RENDER_GRAPH_PASS_HPP
#define ELIX_DEPTH_PREPASS_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class DepthPrepassRenderGraphPass : public IRenderGraphPass
{
public:
    DepthPrepassRenderGraphPass();

    void setup(renderGraph::RGPResourcesBuilder &builder) override;
    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;
    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;
    void cleanup() override;

    void setExtent(VkExtent2D extent);

    RGPResourceHandler &getDepthTextureHandler() { return m_depthTextureHandler; }

private:
    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D   m_scissor{};

    VkFormat m_depthFormat{VK_FORMAT_UNDEFINED};
    VkClearValue m_depthClear{};

    const RenderTarget *m_depthRenderTarget{nullptr};
    RGPResourceHandler  m_depthTextureHandler;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_DEPTH_PREPASS_RENDER_GRAPH_PASS_HPP
