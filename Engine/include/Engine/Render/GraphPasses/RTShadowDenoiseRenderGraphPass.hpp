#ifndef ELIX_RT_SHADOW_DENOISE_RENDER_GRAPH_PASS_HPP
#define ELIX_RT_SHADOW_DENOISE_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"
#include "Core/ShaderHandler.hpp"

#include <glm/glm.hpp>

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class RTShadowDenoiseRenderGraphPass : public IRenderGraphPass
{
public:
    RTShadowDenoiseRenderGraphPass(std::vector<RGPResourceHandler> &rawShadowHandlers,
                                   std::vector<RGPResourceHandler> &normalHandlers,
                                   RGPResourceHandler &depthHandler);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

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
    struct RTShadowDenoisePC
    {
        glm::mat4 invProjection{1.0f};
        glm::vec4 params0{0.0f}; // x=texelSize.x, y=texelSize.y, z=enabled, w=normalSigma
        glm::vec4 params1{0.0f}; // x=depthSigma, y=unused, z=activeRTShadowLayerCount
    };

    static constexpr uint32_t kMaxShadowLights = 16u;

    bool shouldDenoise() const;
    void createComputePipeline();
    void destroyComputePipeline();

    VkFormat m_shadowFormat{VK_FORMAT_R16_SFLOAT};
    VkExtent2D m_extent{};

    std::vector<RGPResourceHandler> &m_rawShadowHandlers;
    std::vector<RGPResourceHandler> &m_normalHandlers;
    RGPResourceHandler &m_depthHandler;

    std::vector<RGPResourceHandler> m_outputHandlers;
    std::vector<const RenderTarget *> m_outputRenderTargets;

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};
    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_shadowSampler{nullptr};
    core::Sampler::SharedPtr m_normalSampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};

    core::ShaderHandler m_computeShader;
    VkPipeline m_computePipeline{VK_NULL_HANDLE};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RT_SHADOW_DENOISE_RENDER_GRAPH_PASS_HPP
