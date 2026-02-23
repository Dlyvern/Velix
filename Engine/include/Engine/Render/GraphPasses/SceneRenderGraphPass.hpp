#ifndef ELIX_SCENE_RENDER_GRAPH_PASS_HPP
#define ELIX_SCENE_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/GraphicsPipeline.hpp"

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class SceneRenderGraphPass : public IRenderGraphPass
{
public:
    SceneRenderGraphPass(renderGraph::RGPResourceHandler &shadowHandler);

    void setup(RGPResourcesBuilder &builder) override;

    void compile(RGPResourcesStorage &storage) override;

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

private:
    std::array<VkClearValue, 3> m_clearValues;

    RGPResourceHandler &m_shadowHandler;
    RGPResourceHandler m_depthTextureHandler;
    RGPResourceHandler m_colorTextureHandler;
    RGPResourceHandler m_objectIdTextureHandler;

    std::vector<const RenderTarget *> m_colorRenderTargets;
    const RenderTarget *m_depthRenderTarget{nullptr};
    const RenderTarget *m_objectIdRenderTarget{nullptr};

    std::vector<VkFormat> m_colorFormats;
    VkFormat m_depthFormat;

    // VkDescriptorPool m_descriptorPool;
    // std::unique_ptr<Skybox> m_skybox{nullptr};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SCENE_RENDER_GRAPH_PASS_HPP