#ifndef ELIX_RT_REFLECTIONS_RENDER_GRAPH_PASS_HPP
#define ELIX_RT_REFLECTIONS_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

// Traces one reflection ray per pixel (more with rtReflectionSamples > 1)
// against the TLAS and composites the result onto the lit image using Fresnel.
class RTReflectionsRenderGraphPass : public IRenderGraphPass
{
public:
    RTReflectionsRenderGraphPass(std::vector<RGPResourceHandler> &lightingHandlers,
                                 std::vector<RGPResourceHandler> &normalHandlers,
                                 std::vector<RGPResourceHandler> &albedoHandlers,
                                 std::vector<RGPResourceHandler> &materialHandlers,
                                 RGPResourceHandler &depthHandler);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    bool isEnabled() const override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    std::vector<RGPResourceHandler> &getOutput() { return m_outputHandlers; }

private:
    std::array<VkClearValue, 1> m_clearValues;
    std::vector<const RenderTarget *> m_outputRenderTargets;

    VkFormat m_colorFormat{VK_FORMAT_R16G16B16A16_SFLOAT};
    VkExtent2D m_extent;
    VkViewport m_viewport;
    VkRect2D m_scissor;

    std::vector<RGPResourceHandler> &m_lightingHandlers;
    std::vector<RGPResourceHandler> &m_normalHandlers;
    std::vector<RGPResourceHandler> &m_albedoHandlers;
    std::vector<RGPResourceHandler> &m_materialHandlers;
    RGPResourceHandler &m_depthHandler;

    std::vector<RGPResourceHandler> m_outputHandlers;

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_textureSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};
};

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END

#endif // ELIX_RT_REFLECTIONS_RENDER_GRAPH_PASS_HPP