#ifndef ELIX_GLASS_RENDER_GRAPH_PASS_HPP
#define ELIX_GLASS_RENDER_GRAPH_PASS_HPP

#include "Core/Macros.hpp"
#include "Core/DescriptorSetLayout.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/RenderTarget.hpp"
#include "Engine/Scene.hpp"

#include <array>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class GlassRenderGraphPass final : public IRenderGraphPass
{
public:
    explicit GlassRenderGraphPass(std::vector<RGPResourceHandler> &colorInputHandlers,
                                  RGPResourceHandler              &depthHandler);

    void setScene(Scene *scene) { m_scene = scene; }
    void setExtent(VkExtent2D extent);

    std::vector<RGPResourceHandler> &getHandlers() { return m_outputHandlers; }

    void setup(RGPResourcesBuilder &builder) override;
    void compile(RGPResourcesStorage &storage) override;
    void record(core::CommandBuffer::SharedPtr cmd,
                const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &ctx) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(
        const RenderGraphPassContext &ctx) const override;

private:
    void recordPassthrough(core::CommandBuffer::SharedPtr cmd, uint32_t imageIndex);
    void recordGlass(core::CommandBuffer::SharedPtr cmd,
                     const RenderGraphPassPerFrameData &data,
                     uint32_t imageIndex);

    Scene *m_scene{nullptr};

    std::vector<RGPResourceHandler> &m_colorInputHandlers;
    RGPResourceHandler              &m_depthHandler;
    std::vector<RGPResourceHandler>  m_outputHandlers;

    std::vector<const RenderTarget *> m_outputRenderTargets;
    std::vector<const RenderTarget *> m_inputRenderTargets;
    const RenderTarget               *m_depthRenderTarget{nullptr};

    VkFormat   m_format{VK_FORMAT_R16G16B16A16_SFLOAT};
    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D   m_scissor{};

    std::array<VkClearValue, 1> m_clearValues{};

    // Passthrough: copies input scene color to output (set 0 = single sampler2D)
    core::DescriptorSetLayout::SharedPtr m_passthroughLayout{nullptr};
    core::PipelineLayout::SharedPtr      m_passthroughPipelineLayout{nullptr};
    std::vector<VkDescriptorSet>         m_passthroughSets;

    // Glass geometry: set 0=camera, set 1=(sceneColor+depth), set 2=per-object
    core::DescriptorSetLayout::SharedPtr m_glassInputLayout{nullptr};
    core::PipelineLayout::SharedPtr      m_glassPipelineLayout{nullptr};
    std::vector<VkDescriptorSet>         m_glassInputSets;

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};

    bool m_descriptorSetsInitialized{false};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_GLASS_RENDER_GRAPH_PASS_HPP
