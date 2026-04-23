#ifndef ELIX_RT_TEMPORAL_ACCUMULATION_RENDER_GRAPH_PASS_HPP
#define ELIX_RT_TEMPORAL_ACCUMULATION_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Core/Image.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"
#include "Core/ShaderHandler.hpp"

#include <glm/glm.hpp>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)








class RTTemporalAccumulationRenderGraphPass : public IRenderGraphPass
{
public:
    RTTemporalAccumulationRenderGraphPass(std::vector<RGPResourceHandler> &inputHandlers,
                                          RGPResourceHandler              &depthHandler,
                                          std::string                      debugName = "RT Temporal Accumulation");

    void record(core::CommandBuffer::SharedPtr        commandBuffer,
                const RenderGraphPassPerFrameData    &data,
                const RenderGraphPassContext         &renderContext) override;

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

    struct TemporalPC
    {
        glm::mat4 invViewProj;
        glm::mat4 prevViewProjection;
    };
    static_assert(sizeof(TemporalPC) == 128);




    struct HistoryPair
    {
        std::shared_ptr<core::Image> images[2];
        VkImageView                  views[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    };

    void createHistoryImages();
    void destroyHistoryImages();
    void transitionHistory(VkCommandBuffer cmd, VkImage image,
                           VkImageLayout from, VkImageLayout to);
    void createComputePipeline();
    void destroyComputePipeline();

    VkFormat   m_format{VK_FORMAT_R16G16B16A16_SFLOAT};
    VkExtent2D m_extent{};

    std::vector<RGPResourceHandler> &m_inputHandlers;
    RGPResourceHandler              &m_depthHandler;

    std::vector<RGPResourceHandler>   m_outputHandlers;
    std::vector<const RenderTarget *> m_outputRenderTargets;

    core::PipelineLayout::SharedPtr      m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};



    std::vector<std::array<VkDescriptorSet, 2>> m_descriptorSets;
    bool                                        m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};

    core::ShaderHandler m_computeShader;
    VkPipeline          m_computePipeline{VK_NULL_HANDLE};

    std::vector<HistoryPair>                         m_historyPairs;
    std::vector<uint32_t>                            m_pingPong;
    std::vector<std::array<VkImageLayout, 2>>        m_historyLayout;



    glm::mat4 m_prevViewProjection{1.0f};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif
