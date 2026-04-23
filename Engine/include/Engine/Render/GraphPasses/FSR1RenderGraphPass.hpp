#ifndef ELIX_FSR1_RENDER_GRAPH_PASS_HPP
#define ELIX_FSR1_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/RenderTarget.hpp"
#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)




class FSR1RenderGraphPass : public IRenderGraphPass
{
public:
    explicit FSR1RenderGraphPass(std::vector<RGPResourceHandler> &inputHandlers);

    void record(core::CommandBuffer::SharedPtr commandBuffer,
                const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    bool isEnabled() const override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;


    void setExtents(VkExtent2D inputExtent, VkExtent2D outputExtent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;
    void freeResources() override;

    std::vector<RGPResourceHandler> &getHandlers() { return m_finalHandlers; }

    struct Outputs
    {
        RGPOutputSlot<MultiHandle> color;
    } outputs;

private:
    std::array<VkClearValue, 1> m_clearValues;



    std::vector<const RenderTarget *> m_intermediateRenderTargets;
    std::vector<const RenderTarget *> m_finalRenderTargets;

    VkFormat m_format;

    std::vector<RGPResourceHandler> &m_inputHandlers;
    std::vector<RGPResourceHandler>  m_intermediateHandlers;
    std::vector<RGPResourceHandler>  m_finalHandlers;

    core::PipelineLayout::SharedPtr      m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};


    std::vector<VkDescriptorSet> m_easuDescriptorSets;
    std::vector<VkDescriptorSet> m_rcasDescriptorSets;
    bool                         m_descriptorSetsInitialized{false};

    VkExtent2D m_inputExtent{0, 0};
    VkExtent2D m_outputExtent{0, 0};
    VkViewport m_viewport;
    VkRect2D   m_scissor;

    core::Sampler::SharedPtr m_sampler{nullptr};
};

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END

#endif
