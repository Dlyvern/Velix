#ifndef ELIX_SSGI_RENDER_GRAPH_PASS_HPP
#define ELIX_SSGI_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/RenderTarget.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class SSGIRenderGraphPass : public IRenderGraphPass
{
public:
    SSGIRenderGraphPass(std::vector<RGPResourceHandler> &litColorHandlers,
                        std::vector<RGPResourceHandler> &normalHandlers,
                        RGPResourceHandler &depthHandler,
                        std::vector<RGPResourceHandler> &materialHandlers,
                        std::vector<RGPResourceHandler> &albedoHandlers);

    void prepareRecord(const RenderGraphPassPerFrameData &data,
                       const RenderGraphPassContext &renderContext) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    bool isEnabled() const override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;
    void cleanup() override;
    void freeResources() override;

    std::vector<RGPResourceHandler> &getOutput() { return m_outputHandlers; }

    struct Outputs
    {
        RGPOutputSlot<MultiHandle> color;
    } outputs;

private:
    std::vector<RGPResourceHandler> &m_litColorHandlers;
    std::vector<RGPResourceHandler> &m_normalHandlers;
    RGPResourceHandler &m_depthHandler;
    std::vector<RGPResourceHandler> &m_materialHandlers;
    std::vector<RGPResourceHandler> &m_albedoHandlers;

    std::vector<RGPResourceHandler> m_outputHandlers;
    std::vector<const RenderTarget *> m_outputTargets;

    VkFormat m_format{VK_FORMAT_R16G16B16A16_SFLOAT};

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};

    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D m_scissor{};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif
