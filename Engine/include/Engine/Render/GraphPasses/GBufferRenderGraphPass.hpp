#ifndef ELIX_GBUFFER_RENDER_GRAPH_PASS_HPP
#define ELIX_GBUFFER_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class GBufferRenderGraphPass : public IRenderGraphPass
{
public:
    explicit GBufferRenderGraphPass(bool enableObjectId = false);

    /// Call before setup() to tell GBuffer to reuse an already-written depth buffer
    /// (e.g. from a DepthPrepassRenderGraphPass) instead of creating its own.
    /// A pointer is stored so that GBuffer's setup() dereferences the live ID
    /// after the prepass has already assigned it via builder.createTexture().
    void setExternalDepthHandler(RGPResourceHandler &handler, RGPResourceHandler *resolvedHandler = nullptr)
    {
        m_externalDepthHandlerPtr = &handler;
        m_externalResolvedDepthHandlerPtr = resolvedHandler;
        m_hasExternalDepth = true;
    }

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);
    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    RGPResourceHandler &getObjectTextureHandler() { return m_objectIdTextureHandler; }
    RGPResourceHandler &getMsaaObjectTextureHandler() { return m_msaaObjectIdTextureHandler; }
    RGPResourceHandler &getDepthTextureHandler() { return m_depthTextureHandler; }
    std::vector<RGPResourceHandler> &getAlbedoTextureHandlers() { return m_albedoTextureHandlers; }
    std::vector<RGPResourceHandler> &getNormalTextureHandlers() { return m_normalTextureHandlers; }
    std::vector<RGPResourceHandler> &getMaterialTextureHandlers() { return m_materialTextureHandlers; }
    std::vector<RGPResourceHandler> &getEmissiveTextureHandlers() { return m_emissiveTextureHandlers; }
    bool usesObjectIdResolvePath() const { return m_msaaObjectIdTextureHandler.isValid(); }

    struct Outputs
    {
        RGPOutputSlot<MultiHandle>  normals;
        RGPOutputSlot<MultiHandle>  albedo;
        RGPOutputSlot<MultiHandle>  material;
        RGPOutputSlot<MultiHandle>  emissive;
        RGPOutputSlot<SingleHandle> depth;
        RGPOutputSlot<SingleHandle> objectId;
    } outputs;

private:
    std::array<VkClearValue, 6> m_clearValues{};

    std::vector<const RenderTarget *> m_normalRenderTargets;
    std::vector<const RenderTarget *> m_msaaNormalRenderTargets;
    std::vector<const RenderTarget *> m_albedoRenderTargets;
    std::vector<const RenderTarget *> m_msaaAlbedoRenderTargets;
    std::vector<const RenderTarget *> m_materialRenderTargets;
    std::vector<const RenderTarget *> m_msaaMaterialRenderTargets;
    std::vector<const RenderTarget *> m_emissiveRenderTargets;
    std::vector<const RenderTarget *> m_msaaEmissiveRenderTargets;
    const RenderTarget *m_depthRenderTarget{nullptr};
    const RenderTarget *m_msaaDepthRenderTarget{nullptr};
    const RenderTarget *m_objectIdRenderTarget{nullptr};
    const RenderTarget *m_msaaObjectIdRenderTarget{nullptr};

    std::vector<VkFormat> m_colorFormats;
    VkFormat m_depthFormat{VK_FORMAT_UNDEFINED};

    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D m_scissor{};

    std::vector<RGPResourceHandler> m_normalTextureHandlers;
    std::vector<RGPResourceHandler> m_msaaNormalTextureHandlers;
    std::vector<RGPResourceHandler> m_albedoTextureHandlers;
    std::vector<RGPResourceHandler> m_msaaAlbedoTextureHandlers;
    std::vector<RGPResourceHandler> m_materialTextureHandlers;
    std::vector<RGPResourceHandler> m_msaaMaterialTextureHandlers;
    std::vector<RGPResourceHandler> m_emissiveTextureHandlers;
    std::vector<RGPResourceHandler> m_msaaEmissiveTextureHandlers;

    RGPResourceHandler m_depthTextureHandler;
    RGPResourceHandler m_msaaDepthTextureHandler;
    RGPResourceHandler m_objectIdTextureHandler;
    RGPResourceHandler m_msaaObjectIdTextureHandler;

    bool m_enableObjectId{false};
    VkSampleCountFlagBits m_rasterizationSamples{VK_SAMPLE_COUNT_1_BIT};

    // External depth prepass support
    bool                m_hasExternalDepth{false};
    RGPResourceHandler *m_externalDepthHandlerPtr{nullptr};
    RGPResourceHandler *m_externalResolvedDepthHandlerPtr{nullptr};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_GBUFFER_RENDER_GRAPH_PASS_HPP
