#ifndef ELIX_RT_GI_DENOISE_RENDER_GRAPH_PASS_HPP
#define ELIX_RT_GI_DENOISE_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Core/Buffer.hpp"
#include "Core/Image.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"
#include "Core/ShaderHandler.hpp"

#include <glm/glm.hpp>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class RTGIDenoiseRenderGraphPass : public IRenderGraphPass
{
public:
    RTGIDenoiseRenderGraphPass(std::vector<RGPResourceHandler> &giHandlers,
                               std::vector<RGPResourceHandler> &normalHandlers,
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
    struct RTGIDenoisePC
    {
        float   texelW;
        float   texelH;
        float   normalSigma;
        float   depthSigma;
        float   stepWidth;
        float   enabled;
        float   _pad[2];
        glm::mat4 invProjection{1.0f};
    };
    static_assert(sizeof(RTGIDenoisePC) == 96);

    void createComputePipeline();
    void destroyComputePipeline();

    // Allocate/free the internal ping-pong images (not tracked by the render graph).
    void createPingPongImages();
    void destroyPingPongImages();

    // Transition a ping-pong image to GENERAL or SHADER_READ_ONLY_OPTIMAL.
    void transitionPingPong(VkCommandBuffer cmd, VkImage image,
                            VkImageLayout from, VkImageLayout to);

    VkFormat   m_format{VK_FORMAT_R16G16B16A16_SFLOAT};
    VkExtent2D m_extent{};

    std::vector<RGPResourceHandler> &m_giHandlers;
    std::vector<RGPResourceHandler> &m_normalHandlers;
    RGPResourceHandler              &m_depthHandler;

    std::vector<RGPResourceHandler>   m_outputHandlers;
    std::vector<const RenderTarget *> m_outputRenderTargets;

    core::PipelineLayout::SharedPtr      m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};

    // Descriptor sets for three dispatches:
    //   pass0: raw GI → pingImage
    //   pass1: pingImage → pongImage
    //   pass2: pongImage → output (graph resource)
    // Each frame index gets 3 descriptor sets.
    std::vector<std::array<VkDescriptorSet, 3>> m_descriptorSets; // [imageIndex][passIdx]
    bool m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};

    core::ShaderHandler m_computeShader;
    VkPipeline          m_computePipeline{VK_NULL_HANDLE};

    // Per-swapchain-image internal ping-pong buffers.
    struct PingPong
    {
        std::shared_ptr<core::Image> pingImage;
        std::shared_ptr<core::Image> pongImage;
        VkImageView pingView{VK_NULL_HANDLE};
        VkImageView pongView{VK_NULL_HANDLE};
    };
    std::vector<PingPong> m_pingPong;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RT_GI_DENOISE_RENDER_GRAPH_PASS_HPP
