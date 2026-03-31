#ifndef ELIX_AUTO_EXPOSURE_RENDER_GRAPH_PASS_HPP
#define ELIX_AUTO_EXPOSURE_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Core/Buffer.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"
#include "Core/ShaderHandler.hpp"

#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

// Histogram-based auto exposure (eye adaptation).
//
// Two compute dispatches per frame:
//   Pass 1 (histogram): samples the HDR scene, builds a 256-bin log-luminance histogram.
//   Pass 2 (average):   reduces the histogram to a weighted-average luminance,
//                       then blends towards the matching exposure with a time-based EMA.
//
// The adapted exposure (float) is stored in a GPU_TO_CPU VkBuffer that is
// read back on the CPU the following frame by TonemapRenderGraphPass, which
// uses it as the push-constant exposure multiplier.  One frame of latency is
// imperceptible for eye-adaptation speeds of 1–5 s⁻¹.
class AutoExposureRenderGraphPass : public IRenderGraphPass
{
public:
    explicit AutoExposureRenderGraphPass(std::vector<RGPResourceHandler> &hdrHandlers);

    void setup(renderGraph::RGPResourcesBuilder &builder) override;
    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer,
                const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext      &renderContext) override;
    void cleanup() override;
    void freeResources() override;

    std::vector<RenderPassExecution> getRenderPassExecutions(
        const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    // Returns the adapted exposure for the given swapchain image (previous frame).
    // Safe to call from the CPU after the frame fence has been signalled.
    float getAdaptedExposure(uint32_t imageIndex) const;

private:
    void createPipelines();
    void destroyPipelines();

    // Push-constant layouts (kept small — within 128-byte guarantee).
    struct HistogramPC
    {
        float minLogLum;
        float rcpLogLumRange;
        uint32_t width;
        uint32_t height;
    };
    static_assert(sizeof(HistogramPC) <= 16);

    struct AveragePC
    {
        float    minLogLum;
        float    logLumRange;
        float    deltaTime;
        float    speedUp;
        float    speedDown;
        float    lowPercent;
        float    highPercent;
        uint32_t pad;
    };
    static_assert(sizeof(AveragePC) <= 32);

    std::vector<RGPResourceHandler> &m_hdrHandlers;
    VkExtent2D                       m_extent{};

    // Per-swapchain-image resources.
    std::vector<core::Buffer::SharedPtr> m_histogramBuffers; // GPU_ONLY,    256 x uint32
    std::vector<core::Buffer::SharedPtr> m_exposureBuffers;  // GPU_TO_CPU,  1  x float
    std::vector<void *>                  m_exposureMapped;   // persistently mapped pointers

    // Descriptor set layout shared by both passes (bindings used differ per pass).
    // Set 0:  b0 = HDR sampler (histogram pass only)
    //         b1 = histogram SSBO
    //         b2 = exposure SSBO
    core::DescriptorSetLayout::SharedPtr m_histDescriptorSetLayout; // b0 HDR + b1 histogram
    core::DescriptorSetLayout::SharedPtr m_avgDescriptorSetLayout;  // b0 histogram + b1 exposure
    core::PipelineLayout::SharedPtr      m_histPipelineLayout;
    core::PipelineLayout::SharedPtr      m_avgPipelineLayout;

    std::vector<VkDescriptorSet> m_histDescriptorSets; // [imageIndex]
    std::vector<VkDescriptorSet> m_avgDescriptorSets;  // [imageIndex]
    bool                         m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_sampler{nullptr};

    core::ShaderHandler m_histShader;
    core::ShaderHandler m_avgShader;
    VkPipeline          m_histPipeline{VK_NULL_HANDLE};
    VkPipeline          m_avgPipeline{VK_NULL_HANDLE};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_AUTO_EXPOSURE_RENDER_GRAPH_PASS_HPP
