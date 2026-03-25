#ifndef ELIX_VOLUMETRIC_FOG_LIGHTING_RENDER_GRAPH_PASS_HPP
#define ELIX_VOLUMETRIC_FOG_LIGHTING_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/RenderTarget.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class VolumetricFogLightingRenderGraphPass : public IRenderGraphPass
{
public:
    VolumetricFogLightingRenderGraphPass(RGPResourceHandler &depthTextureHandler,
                                         RGPResourceHandler &directionalShadowHandler,
                                         RGPResourceHandler &cubeShadowHandler,
                                         RGPResourceHandler &spotShadowHandler);

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

    RGPResourceHandler &m_depthTextureHandler;
    RGPResourceHandler &m_directionalShadowHandler;
    RGPResourceHandler &m_cubeShadowHandler;
    RGPResourceHandler &m_spotShadowHandler;

    std::vector<RGPResourceHandler> m_outputHandlers;
    std::vector<const RenderTarget *> m_outputTargets;

    VkFormat m_format{VK_FORMAT_R16G16B16A16_SFLOAT};

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};
    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_depthSampler{nullptr};
    core::Sampler::SharedPtr m_shadowSampler{nullptr};

    VkExtent2D m_fullExtent{};
    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D m_scissor{};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_VOLUMETRIC_FOG_LIGHTING_RENDER_GRAPH_PASS_HPP
