#ifndef ELIX_MATERIAL_PREVIEW_RENDER_GRAPH_PASS_HPP
#define ELIX_MATERIAL_PREVIEW_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Core/GraphicsPipeline.hpp"
#include "Core/RenderPass.hpp"
#include "Core/Framebuffer.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class MaterialPreviewRenderGraphPass : public IRenderGraphPass
{
public:
    MaterialPreviewRenderGraphPass(VkExtent2D extent);

    void setup(renderGraph::RGPResourcesBuilder &builder) override;
    void compile(RGPResourcesStorage &storage) override;
    void execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data) override;
    void update(const RenderGraphPassContext &renderData) override;
    void getRenderPassBeginInfo(VkRenderPassBeginInfo &renderPassBeginInfo) const override;

    void beginRenderPass() {}
    void endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer) {}

    void cleanup() {}

    void onSwapChainResized(renderGraph::RGPResourcesStorage &storage) {}

    const RenderTarget *getRenderTarget() const
    {
        return m_colorRenderTarget;
    }

private:
    VkViewport m_viewport;
    VkRect2D m_scissor;
    VkExtent2D m_extent;
    uint32_t m_currentFrame{0};
    std::array<VkClearValue, 1> m_clearValues;

    core::RenderPass::SharedPtr m_renderPass{nullptr};
    core::GraphicsPipeline::SharedPtr m_graphicsPipeline{nullptr};
    core::Framebuffer::SharedPtr m_framebuffer;

    const RenderTarget *m_colorRenderTarget{nullptr};

    RGPResourceHandler m_colorTextureHandler;

    GPUMesh::SharedPtr m_gpuMesh{nullptr};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_MATERIAL_PREVIEW_RENDER_GRAPH_PASS_HPP