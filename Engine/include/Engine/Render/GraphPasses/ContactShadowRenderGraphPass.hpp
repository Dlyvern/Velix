#ifndef ELIX_CONTACT_SHADOW_RENDER_GRAPH_PASS_HPP
#define ELIX_CONTACT_SHADOW_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/RenderTarget.hpp"
#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class ContactShadowRenderGraphPass : public IRenderGraphPass
{
public:
    ContactShadowRenderGraphPass(std::vector<RGPResourceHandler> &hdrInputHandlers,
                                 std::vector<RGPResourceHandler> &normalHandlers,
                                 RGPResourceHandler              &depthHandler);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    bool isEnabled() const override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;
    void freeResources() override;

    std::vector<RGPResourceHandler> &getOutput() { return m_outputHandlers; }

    struct Outputs
    {
        RGPOutputSlot<MultiHandle> color;
    } outputs;

private:
    std::array<VkClearValue, 1> m_clearValues;

    std::vector<const RenderTarget *> m_outputRenderTargets;

    VkFormat m_colorFormat{VK_FORMAT_R16G16B16A16_SFLOAT};

    std::vector<RGPResourceHandler> &m_hdrInputHandlers;
    std::vector<RGPResourceHandler> &m_normalHandlers;
    RGPResourceHandler              &m_depthHandler;
    std::vector<RGPResourceHandler>  m_outputHandlers;

    core::PipelineLayout::SharedPtr      m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_textureSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsInitialized{false};

    VkExtent2D m_extent;
    VkViewport m_viewport;
    VkRect2D   m_scissor;

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};
};

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END

#endif // ELIX_CONTACT_SHADOW_RENDER_GRAPH_PASS_HPP
