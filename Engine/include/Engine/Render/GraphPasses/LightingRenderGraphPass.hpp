#ifndef ELIX_LIGHTING_RENDER_GRAPH_PASS_HPP
#define ELIX_LIGHTING_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Texture.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <glm/glm.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class LightingRenderGraphPass : public IRenderGraphPass
{
public:
    LightingRenderGraphPass(RGPResourceHandler &shadowTextureHandler,
                            RGPResourceHandler &depthTextureHandler, RGPResourceHandler &cubeTextureHandler, RGPResourceHandler &arrayTextureHandler,
                            std::vector<RGPResourceHandler> &albedoTextureHandlers,
                            std::vector<RGPResourceHandler> &normalTextureHandlers,
                            std::vector<RGPResourceHandler> &materialTextureHandlers,
                            std::vector<RGPResourceHandler> &emissiveTextureHandlers,
                            std::vector<RGPResourceHandler> *rtShadowTextureHandlers = nullptr,
                            std::vector<RGPResourceHandler> *aoTextureHandlers = nullptr);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    // Set the active reflection probe for this pass. Pass VK_NULL_HANDLE view to disable.
    void setProbeData(VkImageView view, VkSampler sampler,
                      const glm::vec3 &worldPos, float radius, float intensity);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    std::vector<RGPResourceHandler> &getOutput()
    {
        return m_colorTextureHandler;
    }

    struct Outputs
    {
        RGPOutputSlot<MultiHandle> color;
    } outputs;

private:
    std::array<VkClearValue, 1> m_clearValues;

    std::vector<const RenderTarget *> m_colorRenderTargets;

    VkFormat m_colorFormat;

    VkExtent2D m_extent;
    VkViewport m_viewport;
    VkRect2D m_scissor;

    std::vector<RGPResourceHandler> m_colorTextureHandler;

    RGPResourceHandler &m_shadowTextureHandler;
    RGPResourceHandler &m_depthTextureHandler;
    RGPResourceHandler &m_cubeTextureHandler;
    RGPResourceHandler &m_arrayTextureHandler;

    std::vector<RGPResourceHandler> &m_albedoTextureHandlers;
    std::vector<RGPResourceHandler> &m_normalTextureHandlers;
    std::vector<RGPResourceHandler> &m_materialTextureHandlers;
    std::vector<RGPResourceHandler> &m_emissiveTextureHandlers;

    std::vector<RGPResourceHandler> *m_rtShadowTextureHandlers{nullptr}; // optional, binding 10
    std::vector<RGPResourceHandler> *m_aoTextureHandlers{nullptr}; // optional, binding 9

    core::Sampler::SharedPtr m_defaultSampler{nullptr};
    core::Sampler::SharedPtr m_sampler{nullptr};
    Texture::SharedPtr m_defaultWhiteTexture{nullptr};

    // Reflection probe state (updated per-frame by EditorRuntime)
    VkImageView m_probeImageView{VK_NULL_HANDLE};
    VkSampler   m_probeSampler{VK_NULL_HANDLE};
    glm::vec3   m_probeWorldPos{0.0f};
    float       m_probeRadius{0.0f};
    float       m_probeIntensity{0.0f};
    bool        m_probeDescriptorDirty{false};

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_descriptorSets{VK_NULL_HANDLE};
    bool m_descriptorSetsInitialized{false};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_LIGHTING_RENDER_GRAPH_PASS_HPP
