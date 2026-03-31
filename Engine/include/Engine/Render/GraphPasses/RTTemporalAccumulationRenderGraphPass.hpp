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

// Camera-based temporal accumulation for RT effects (GI, Reflections).
// Reprojects previous frame via view-projection matrices, neighbourhood-clamps
// the history, and blends 10% new / 90% accumulated.
//
// Each swapchain image index gets two ping-pong history images. The pass owns
// these images directly (not render-graph resources) so they persist across
// frames without being recreated on recompile.
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
    // Push-constant block — exactly 128 bytes (Vulkan minimum guarantee).
    struct TemporalPC
    {
        glm::mat4 invViewProj;        // NDC → world  (current frame)
        glm::mat4 prevViewProjection; // world → prev-frame NDC
    };
    static_assert(sizeof(TemporalPC) == 128);

    // Two persistent history images per swapchain index for ping-pong.
    // Frame N  (image i): read images[i][pingPong],   write images[i][1-pingPong]
    // Frame N+1(image i): read images[i][1-pingPong], write images[i][pingPong]
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

    // [imageIndex][pingPong 0|1] — two descriptor sets per swapchain image,
    // one per ping-pong state, so no per-frame descriptor updates are needed.
    std::vector<std::array<VkDescriptorSet, 2>> m_descriptorSets;
    bool                                        m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};

    core::ShaderHandler m_computeShader;
    VkPipeline          m_computePipeline{VK_NULL_HANDLE};

    std::vector<HistoryPair>                         m_historyPairs;  // [imageIndex]
    std::vector<uint32_t>                            m_pingPong;      // [imageIndex] current state (0|1)
    std::vector<std::array<VkImageLayout, 2>>        m_historyLayout; // [imageIndex][0|1] current layout

    // Previous-frame view-projection stored by this pass to avoid
    // adding extra fields to RenderGraphPassPerFrameData.
    glm::mat4 m_prevViewProjection{1.0f};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RT_TEMPORAL_ACCUMULATION_RENDER_GRAPH_PASS_HPP
