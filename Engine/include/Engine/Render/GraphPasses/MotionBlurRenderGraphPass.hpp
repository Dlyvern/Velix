#ifndef ELIX_MOTION_BLUR_RENDER_GRAPH_PASS_HPP
#define ELIX_MOTION_BLUR_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/RenderTarget.hpp"
#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class MotionBlurRenderGraphPass : public IRenderGraphPass
{
public:
    explicit MotionBlurRenderGraphPass(std::vector<RGPResourceHandler> &inputHandlers,
                                       RGPResourceHandler              &depthHandler);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    bool isEnabled() const override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;
    void freeResources() override;

    std::vector<RGPResourceHandler> &getHandlers() { return m_outputHandlers; }

    struct Outputs
    {
        RGPOutputSlot<MultiHandle> color;
    } outputs;

private:
    std::array<VkClearValue, 1> m_clearValues;

    std::vector<const RenderTarget *> m_outputRenderTargets;

    VkFormat m_format;

    std::vector<RGPResourceHandler> &m_inputHandlers;
    RGPResourceHandler              &m_depthHandler;
    std::vector<RGPResourceHandler>  m_outputHandlers;

    core::PipelineLayout::SharedPtr      m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_descriptorSets;
    bool                         m_descriptorSetsInitialized{false};

    VkExtent2D m_extent;
    VkViewport m_viewport;
    VkRect2D   m_scissor;

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};

    glm::mat4 m_prevView{1.0f};
    glm::mat4 m_prevProjection{1.0f};
    bool      m_hasPrevFrame{false};
};

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END

#endif // ELIX_MOTION_BLUR_RENDER_GRAPH_PASS_HPP
