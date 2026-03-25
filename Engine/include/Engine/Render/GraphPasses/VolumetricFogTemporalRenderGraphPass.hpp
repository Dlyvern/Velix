#ifndef ELIX_VOLUMETRIC_FOG_TEMPORAL_RENDER_GRAPH_PASS_HPP
#define ELIX_VOLUMETRIC_FOG_TEMPORAL_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/RenderTarget.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class VolumetricFogTemporalRenderGraphPass : public IRenderGraphPass
{
public:
    VolumetricFogTemporalRenderGraphPass(std::vector<RGPResourceHandler> &inputHandlers,
                                         RGPResourceHandler &depthHandler);

    void setup(renderGraph::RGPResourcesBuilder &builder) override;
    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer,
                const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(
        const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);
    void freeResources() override;

    std::vector<RGPResourceHandler> &getOutput() { return m_outputHandlers; }

    struct Outputs
    {
        RGPOutputSlot<MultiHandle> color;
    } outputs;

private:
    void updateInternalExtent();

    std::array<VkClearValue, 1> m_clearValues{};

    std::vector<RGPResourceHandler> &m_inputHandlers;
    RGPResourceHandler &m_depthHandler;
    std::vector<RGPResourceHandler> m_outputHandlers;
    std::vector<const RenderTarget *> m_outputTargets;

    RenderTarget::SharedPtr m_historyTarget{nullptr};
    bool m_historyInitialized{false};
    bool m_hasPrevFrame{false};
    glm::mat4 m_prevView{1.0f};
    glm::mat4 m_prevProjection{1.0f};
    size_t m_prevFogSettingsHash{0u};

    VkFormat m_format{VK_FORMAT_R16G16B16A16_SFLOAT};

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};
    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};

    VkExtent2D m_fullExtent{};
    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D m_scissor{};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_VOLUMETRIC_FOG_TEMPORAL_RENDER_GRAPH_PASS_HPP
