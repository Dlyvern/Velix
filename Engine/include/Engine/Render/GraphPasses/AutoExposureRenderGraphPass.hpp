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



    float getAdaptedExposure(uint32_t imageIndex) const;

private:
    void createPipelines();
    void destroyPipelines();


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


    std::vector<core::Buffer::SharedPtr> m_histogramBuffers;
    std::vector<core::Buffer::SharedPtr> m_exposureBuffers;
    std::vector<void *>                  m_exposureMapped;





    core::DescriptorSetLayout::SharedPtr m_histDescriptorSetLayout;
    core::DescriptorSetLayout::SharedPtr m_avgDescriptorSetLayout;
    core::PipelineLayout::SharedPtr      m_histPipelineLayout;
    core::PipelineLayout::SharedPtr      m_avgPipelineLayout;

    std::vector<VkDescriptorSet> m_histDescriptorSets;
    std::vector<VkDescriptorSet> m_avgDescriptorSets;
    bool                         m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_sampler{nullptr};

    core::ShaderHandler m_histShader;
    core::ShaderHandler m_avgShader;
    VkPipeline          m_histPipeline{VK_NULL_HANDLE};
    VkPipeline          m_avgPipeline{VK_NULL_HANDLE};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif
