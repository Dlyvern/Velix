#ifndef ELIX_SCENE_RENDER_GRAPH_PASS_HPP
#define ELIX_SCENE_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Framebuffer.hpp"
#include "Core/GraphicsPipeline.hpp"

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class SceneRenderGraphPass : public IRenderGraphPass
{
public:
    // SceneRenderGraphPass(renderGraph::RGPResourceHandler &shadowHandler);
    SceneRenderGraphPass();

    void setup(RGPResourcesBuilder &builder) override;

    void compile(RGPResourcesStorage &storage) override;

    void execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data) override;

    void update(const RenderGraphPassContext &renderData) override;

    void getRenderPassBeginInfo(VkRenderPassBeginInfo &renderPassBeginInfo) const override;

    void onSwapChainResized(renderGraph::RGPResourcesStorage &storage) override;

private:
    void createGraphicsPipeline();

    std::array<VkClearValue, 2> m_clearValues;

    core::GraphicsPipeline::SharedPtr m_graphicsPipeline{nullptr};

    std::vector<core::Framebuffer::SharedPtr> m_framebuffers;
    core::RenderPass::SharedPtr m_renderPass{nullptr};

    uint32_t m_imageIndex{0};
    uint32_t m_currentFrame{0};

    // renderGraph::RGPResourceHandler &m_shadowHandler;
    renderGraph::RGPResourceHandler m_depthTextureHandler;
    renderGraph::RGPResourceHandler m_colorTextureHandler;

    // VkDescriptorPool m_descriptorPool;
    // std::unique_ptr<Skybox> m_skybox{nullptr};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SCENE_RENDER_GRAPH_PASS_HPP