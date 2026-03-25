#ifndef ELIX_RT_REFLECTIONS_RENDER_GRAPH_PASS_HPP
#define ELIX_RT_REFLECTIONS_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Core/Buffer.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"
#include "Core/ShaderHandler.hpp"
#include "Engine/Skybox.hpp"
#include "Engine/Texture.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

// Traces one reflection ray per pixel (more with rtReflectionSamples > 1)
// against the TLAS and composites the result onto the lit image using Fresnel.
class RTReflectionsRenderGraphPass : public IRenderGraphPass
{
public:
    RTReflectionsRenderGraphPass(std::vector<RGPResourceHandler> &lightingHandlers,
                                 std::vector<RGPResourceHandler> &normalHandlers,
                                 std::vector<RGPResourceHandler> &albedoHandlers,
                                 std::vector<RGPResourceHandler> &materialHandlers,
                                 RGPResourceHandler &depthHandler);

    void prepareRecord(const RenderGraphPassPerFrameData &data,
                       const RenderGraphPassContext &renderContext) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    bool isEnabled() const override;
    uint64_t getExecutionCacheKey(const RenderGraphPassContext &renderContext) const override;

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
    std::array<VkClearValue, 1> m_clearValues;
    std::vector<const RenderTarget *> m_outputRenderTargets;

    VkFormat m_colorFormat{VK_FORMAT_R16G16B16A16_SFLOAT};
    VkExtent2D m_extent;
    VkViewport m_viewport;
    VkRect2D m_scissor;

    std::vector<RGPResourceHandler> &m_lightingHandlers;
    std::vector<RGPResourceHandler> &m_normalHandlers;
    std::vector<RGPResourceHandler> &m_albedoHandlers;
    std::vector<RGPResourceHandler> &m_materialHandlers;
    RGPResourceHandler &m_depthHandler;

    std::vector<RGPResourceHandler> m_outputHandlers;

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_textureSetLayout{nullptr};

    // Separate layout for the RT pipeline path: set 0 = camera, set 1 = RT textures, set 2 = bindless.
    VkPipelineLayout m_rtPipelineLayout{VK_NULL_HANDLE};

    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};
    std::vector<core::Buffer::SharedPtr> m_reflectionSceneBuffers;
    std::vector<VkDeviceSize> m_reflectionSceneBufferSizes;
    Texture::SharedPtr m_fallbackEnvironmentTexture{nullptr};
    std::unique_ptr<Skybox> m_environmentSkybox{nullptr};
    std::string m_requestedSkyboxHDRPath;
    std::string m_loadedSkyboxHDRPath;
    bool m_pendingSkyboxUpdate{true};

    core::ShaderHandler m_raygenShader;
    core::ShaderHandler m_missShader;
    core::ShaderHandler m_closestHitShader;
    core::Buffer::SharedPtr m_shaderBindingTable{nullptr};

    VkPipeline m_rayTracingPipeline{VK_NULL_HANDLE};
    VkStridedDeviceAddressRegionKHR m_raygenRegion{};
    VkStridedDeviceAddressRegionKHR m_missRegion{};
    VkStridedDeviceAddressRegionKHR m_hitRegion{};
    VkStridedDeviceAddressRegionKHR m_callableRegion{};

    bool canUsePipelinePath() const;
    bool shouldUsePipelinePath() const;
    void createRayTracingPipeline();
    void createShaderBindingTable(uint32_t groupCount);
    void destroyRayTracingPipeline();
    void updateReflectionSceneBuffer(const RenderGraphPassPerFrameData &data, uint32_t frameIndex);
    void ensureFallbackEnvironmentTexture();
    void updateEnvironmentSkybox();
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RT_REFLECTIONS_RENDER_GRAPH_PASS_HPP
