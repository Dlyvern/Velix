#ifndef ELIX_SKY_LIGHT_RENDER_GRAPH_PASS_HPP
#define ELIX_SKY_LIGHT_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/RenderTarget.hpp"
#include "Engine/SkyLightSystem.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class SkyLightRenderGraphPass : public IRenderGraphPass
{
public:
    SkyLightRenderGraphPass(uint32_t lightingId, uint32_t gbufferId,
                            std::vector<RGPResourceHandler> &lightingInputHandlers,
                            RGPResourceHandler &depthTextureHandler);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    std::vector<RGPResourceHandler> &getOutput()
    {
        return m_colorTextureHandler;
    }

private:
    std::array<VkClearValue, 1> m_clearValues;

    std::vector<const RenderTarget *> m_colorRenderTargets;
    const RenderTarget *m_depthRenderTarget{nullptr};

    VkFormat m_colorFormat{VK_FORMAT_UNDEFINED};
    VkFormat m_depthFormat{VK_FORMAT_UNDEFINED};

    std::vector<RGPResourceHandler> &m_lightingInputHandlers;
    RGPResourceHandler &m_depthTextureHandler;
    std::vector<RGPResourceHandler> m_colorTextureHandler;

    core::PipelineLayout::SharedPtr m_copyPipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_copyDescriptorSetLayout{nullptr};
    std::vector<VkDescriptorSet> m_copyDescriptorSets{VK_NULL_HANDLE};

    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D m_scissor{};

    core::Sampler::SharedPtr m_defaultSampler{nullptr};

    std::unique_ptr<SkyLightSystem> m_skyLightSystem{nullptr};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SKY_LIGHT_RENDER_GRAPH_PASS_HPP
