#ifndef ELIX_RT_INDIRECT_DIFFUSE_RENDER_GRAPH_PASS_HPP
#define ELIX_RT_INDIRECT_DIFFUSE_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Core/Buffer.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/RTX/RayTracingPipeline.hpp"
#include "Core/RTX/ShaderBindingTable.hpp"
#include "Core/Sampler.hpp"
#include "Core/ShaderHandler.hpp"

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class RTIndirectDiffuseRenderGraphPass : public IRenderGraphPass
{
public:
    RTIndirectDiffuseRenderGraphPass(std::vector<RGPResourceHandler> &normalHandlers,
                                     std::vector<RGPResourceHandler> &albedoHandlers,
                                     RGPResourceHandler              &depthHandler);

    void record(core::CommandBuffer::SharedPtr commandBuffer,
                const RenderGraphPassPerFrameData  &data,
                const RenderGraphPassContext       &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(
        const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder   &builder) override;
    void cleanup() override;
    void freeResources() override;

    std::vector<RGPResourceHandler> &getOutput() { return m_outputHandlers; }

    struct Outputs
    {
        RGPOutputSlot<MultiHandle> color;
    } outputs;

private:
    struct RTIndirectDiffusePC
    {
        int   giSamples;
        float sunHeight;
        float frameOffset;
        float pad;
    };
    static_assert(sizeof(RTIndirectDiffusePC) == 16);

    VkFormat   m_format{VK_FORMAT_R16G16B16A16_SFLOAT};
    VkExtent2D m_extent{};

    std::vector<RGPResourceHandler> &m_normalHandlers;
    std::vector<RGPResourceHandler> &m_albedoHandlers;
    RGPResourceHandler              &m_depthHandler;

    std::vector<RGPResourceHandler>      m_outputHandlers;
    std::vector<const RenderTarget *>    m_outputRenderTargets;

    core::PipelineLayout::SharedPtr      m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_textureSetLayout{nullptr};
    std::vector<VkDescriptorSet>         m_descriptorSets;
    bool                                 m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};

    // Per-frame instance SSBO (vertex/index addresses + material) for the rchit shader.
    std::vector<core::Buffer::SharedPtr> m_giSceneBuffers;
    std::vector<VkDeviceSize>            m_giSceneBufferSizes;

    // Separate 3-set pipeline layout (set 0=camera, set 1=textures, set 2=bindless).
    VkPipelineLayout m_rtPipelineLayout{VK_NULL_HANDLE};

    core::ShaderHandler m_raygenShader;
    core::ShaderHandler m_missShader;
    core::ShaderHandler m_closestHitShader;
    core::rtx::ShaderBindingTable::SharedPtr m_shaderBindingTable{nullptr};

    core::rtx::RayTracingPipeline::SharedPtr m_rayTracingPipeline{nullptr};

    uint32_t m_frameCounter{0};

    bool canUsePipelinePath() const;
    void createRayTracingPipeline();
    void createShaderBindingTable();
    void destroyRayTracingPipeline();
    void updateGISceneBuffer(const RenderGraphPassPerFrameData &data, uint32_t frameIndex);
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RT_INDIRECT_DIFFUSE_RENDER_GRAPH_PASS_HPP
