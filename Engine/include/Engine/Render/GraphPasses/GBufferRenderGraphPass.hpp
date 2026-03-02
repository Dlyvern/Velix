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
    enum class ExecutionVariant : uint8_t
    {
        MAIN = 0,
        OBJECT_ONLY = 1
    };

    std::array<VkClearValue, 5> m_clearValues;

    std::vector<const RenderTarget *> m_normalRenderTargets;
    std::vector<const RenderTarget *> m_albedoRenderTargets;
    std::vector<const RenderTarget *> m_materialRenderTargets;

    std::vector<const RenderTarget *> m_normalMsaaRenderTargets;
    std::vector<const RenderTarget *> m_albedoMsaaRenderTargets;
    std::vector<const RenderTarget *> m_materialMsaaRenderTargets;

    const RenderTarget *m_depthRenderTarget{nullptr};
    const RenderTarget *m_objectIdRenderTarget{nullptr};
    const RenderTarget *m_depthMsaaRenderTarget{nullptr};
    const RenderTarget *m_objectIdMsaaRenderTarget{nullptr};

    std::vector<VkFormat> m_colorFormats;
    VkFormat m_depthFormat;
    VkSampleCountFlagBits m_activeMsaaSamples{VK_SAMPLE_COUNT_1_BIT};
    VkSampleCountFlagBits m_requestedMsaaSamples{VK_SAMPLE_COUNT_1_BIT};
    bool m_useMsaa{false};

    VkExtent2D m_extent;
    VkViewport m_viewport;
    VkRect2D m_scissor;

    std::vector<RGPResourceHandler> m_normalTextureHandlers;
    std::vector<RGPResourceHandler> m_albedoTextureHandlers;
    std::vector<RGPResourceHandler> m_materialTextureHandlers;
    std::vector<RGPResourceHandler> m_normalMsaaTextureHandlers;
    std::vector<RGPResourceHandler> m_albedoMsaaTextureHandlers;
    std::vector<RGPResourceHandler> m_materialMsaaTextureHandlers;

    RGPResourceHandler m_depthTextureHandler;
    RGPResourceHandler m_objectIdTextureHandler;
    RGPResourceHandler m_depthMsaaTextureHandler;
    RGPResourceHandler m_objectIdMsaaTextureHandler;

    renderGraph::RGPResourcesBuilder *m_resourcesBuilder{nullptr};

    mutable uint32_t m_currentExecutionIndex{0};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_GBUFFER_RENDER_GRAPH_PASS_HPP
