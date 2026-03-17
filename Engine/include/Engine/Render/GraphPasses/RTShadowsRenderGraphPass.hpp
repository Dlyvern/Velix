#ifndef ELIX_RT_SHADOWS_RENDER_GRAPH_PASS_HPP
#define ELIX_RT_SHADOWS_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Core/Buffer.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"
#include "Core/ShaderHandler.hpp"

#include <array>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class RTShadowsRenderGraphPass : public IRenderGraphPass
{
public:
    RTShadowsRenderGraphPass(std::vector<RGPResourceHandler> &normalHandlers,
                             RGPResourceHandler &depthHandler);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;
    void cleanup() override;

    std::vector<RGPResourceHandler> &getOutput() { return m_outputHandlers; }

private:
    struct RTShadowsPC
    {
        float enableRTShadows;
        float rtShadowSamples;
        float rtShadowPenumbraSize;
        float activeRTShadowLayerCount;
    };

    static constexpr uint32_t kMaxShadowLights = 16u;

    VkFormat m_shadowFormat{VK_FORMAT_R16_SFLOAT};
    VkExtent2D m_extent{};

    std::vector<RGPResourceHandler> &m_normalHandlers;
    RGPResourceHandler &m_depthHandler;
    std::vector<RGPResourceHandler> m_outputHandlers;
    std::vector<const RenderTarget *> m_outputRenderTargets;

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_textureSetLayout{nullptr};
    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};

    core::ShaderHandler m_raygenShader;
    core::ShaderHandler m_missShader;
    core::ShaderHandler m_closestHitShader;
    core::Buffer::SharedPtr m_shaderBindingTable{nullptr};

    VkPipeline m_rayTracingPipeline{VK_NULL_HANDLE};
    VkStridedDeviceAddressRegionKHR m_raygenRegion{};
    VkStridedDeviceAddressRegionKHR m_missRegion{};
    VkStridedDeviceAddressRegionKHR m_hitRegion{};
    VkStridedDeviceAddressRegionKHR m_callableRegion{};

    bool shouldUsePipelinePath() const;
    bool canUsePipelinePath() const;
    void createRayTracingPipeline();
    void createShaderBindingTable(uint32_t groupCount);
    void destroyRayTracingPipeline();

    VkPipeline       m_computePipeline{VK_NULL_HANDLE};
    VkPipelineLayout m_computePipelineLayout{VK_NULL_HANDLE};
    core::ShaderHandler m_computeShader;

    bool shouldUseRayQueryPath() const;
    void createComputePipeline();
    void destroyComputePipeline();
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RT_SHADOWS_RENDER_GRAPH_PASS_HPP
