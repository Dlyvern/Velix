#ifndef ELIX_GBUFFER_RENDER_GRAPH_PASS_HPP
#define ELIX_GBUFFER_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class GBufferRenderGraphPass : public IRenderGraphPass
{
public:
    GBufferRenderGraphPass();

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    RGPResourceHandler &getObjectTextureHandler()
    {
        return m_objectIdTextureHandler;
    }

    RGPResourceHandler &getDepthTextureHandler()
    {
        return m_depthTextureHandler;
    }

    std::vector<RGPResourceHandler> &getAlbedoTextureHandlers()
    {
        return m_albedoTextureHandlers;
    }

    std::vector<RGPResourceHandler> &getNormalTextureHandlers()
    {
        return m_normalTextureHandlers;
    }

    std::vector<RGPResourceHandler> &getMaterialTextureHandlers()
    {
        return m_materialTextureHandlers;
    }

private:
    std::array<VkClearValue, 5> m_clearValues;

    std::vector<const RenderTarget *> m_normalRenderTargets;
    std::vector<const RenderTarget *> m_albedoRenderTargets;
    std::vector<const RenderTarget *> m_materialRenderTargets;

    const RenderTarget *m_depthRenderTarget{nullptr};
    const RenderTarget *m_objectIdRenderTarget{nullptr};

    std::vector<VkFormat> m_colorFormats;
    VkFormat m_depthFormat;

    VkExtent2D m_extent;
    VkViewport m_viewport;
    VkRect2D m_scissor;

    std::vector<RGPResourceHandler> m_normalTextureHandlers;
    std::vector<RGPResourceHandler> m_albedoTextureHandlers;
    std::vector<RGPResourceHandler> m_materialTextureHandlers;

    RGPResourceHandler m_depthTextureHandler;
    RGPResourceHandler m_objectIdTextureHandler;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_GBUFFER_RENDER_GRAPH_PASS_HPP