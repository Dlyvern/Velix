#ifndef ELIX_TAA_RENDER_GRAPH_PASS_HPP
#define ELIX_TAA_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/RenderTarget.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class TAARenderGraphPass : public IRenderGraphPass
{
public:
    explicit TAARenderGraphPass(std::vector<RGPResourceHandler> &inputHandlers);

    void setup(renderGraph::RGPResourcesBuilder &builder) override;
    void compile(renderGraph::RGPResourcesStorage &storage) override;

    void record(core::CommandBuffer::SharedPtr commandBuffer,
                const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    bool isEnabled() const override;

    std::vector<RenderPassExecution> getRenderPassExecutions(
        const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);
    void freeResources() override;

    std::vector<RGPResourceHandler> &getHandlers() { return m_outputHandlers; }

    struct Outputs
    {
        RGPOutputSlot<MultiHandle> color;
    } outputs;

private:
    void initHistory(core::CommandBuffer::SharedPtr commandBuffer);

    std::vector<RGPResourceHandler> &m_inputHandlers;
    std::vector<RGPResourceHandler> m_outputHandlers;

    std::vector<const RenderTarget *> m_outputTargets;

    // Persistent history – one texture shared across all swapchain images
    RenderTarget::SharedPtr m_historyTarget{nullptr};
    bool m_historyInitialized{false};

    VkFormat m_format{VK_FORMAT_UNDEFINED};

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_sampler{nullptr};

    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D m_scissor{};

    std::array<VkClearValue, 1> m_clearValues{};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_TAA_RENDER_GRAPH_PASS_HPP
