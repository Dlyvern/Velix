#ifndef ELIX_SSAO_RENDER_GRAPH_PASS_HPP
#define ELIX_SSAO_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/RenderTarget.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Buffer.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

/// Screen-Space Ambient Occlusion pass.
/// Reads GBuffer depth + normals, outputs a single-channel R8 AO texture.
/// Lighting pass reads it at binding 7 and multiplies with ambient term.
class SSAORenderGraphPass : public IRenderGraphPass
{
public:
    explicit SSAORenderGraphPass(RGPResourceHandler &depthHandler,
                                 std::vector<RGPResourceHandler> &normalHandlers);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    /// Returns per-image AO output handlers so the Lighting pass can read them.
    std::vector<RGPResourceHandler> &getAOHandlers() { return m_outputHandlers; }

private:
    RGPResourceHandler              &m_depthHandler;
    std::vector<RGPResourceHandler> &m_normalHandlers;

    std::vector<RGPResourceHandler>   m_outputHandlers;
    std::vector<const RenderTarget *> m_outputTargets;

    VkFormat m_format{VK_FORMAT_R8_UNORM};

    core::PipelineLayout::SharedPtr      m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsInitialized{false};

    // Kernel + noise UBO
    core::Buffer::SharedPtr m_kernelBuffer{nullptr};
    void *m_kernelMapped{nullptr};

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_noiseSampler{nullptr};

    VkExtent2D m_extent;
    VkViewport m_viewport;
    VkRect2D   m_scissor;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SSAO_RENDER_GRAPH_PASS_HPP
