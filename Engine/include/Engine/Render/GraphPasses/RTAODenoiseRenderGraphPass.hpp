#ifndef ELIX_RTAO_DENOISE_RENDER_GRAPH_PASS_HPP
#define ELIX_RTAO_DENOISE_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"
#include "Core/ShaderHandler.hpp"

#include <glm/glm.hpp>

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class RTAODenoiseRenderGraphPass : public IRenderGraphPass
{
public:
    RTAODenoiseRenderGraphPass(std::vector<RGPResourceHandler> &rawAOHandlers,
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
        RGPOutputSlot<MultiHandle> ao;
    } outputs;

private:
    struct RTAODenoisePC
    {
        glm::mat4 invProjection{1.0f};
        glm::vec4 params0{0.0f}; // x=texelSize.x, y=texelSize.y, z=enabled, w=normalSigma
        glm::vec4 params1{0.0f}; // x=depthSigma
    };

    bool shouldDenoise() const;
    void createComputePipeline();
    void destroyComputePipeline();

    VkFormat   m_aoFormat{VK_FORMAT_R8_UNORM};
    VkExtent2D m_extent{};

    std::vector<RGPResourceHandler> &m_rawAOHandlers;
    std::vector<RGPResourceHandler> &m_normalHandlers;
    RGPResourceHandler              &m_depthHandler;

    std::vector<RGPResourceHandler>    m_outputHandlers;
    std::vector<const RenderTarget *>  m_outputRenderTargets;

    core::PipelineLayout::SharedPtr       m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr  m_descriptorSetLayout{nullptr};
    std::vector<VkDescriptorSet>          m_descriptorSets;
    bool                                  m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_aoSampler{nullptr};
    core::Sampler::SharedPtr m_normalSampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};

    core::ShaderHandler m_computeShader;
    VkPipeline          m_computePipeline{VK_NULL_HANDLE};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RTAO_DENOISE_RENDER_GRAPH_PASS_HPP
