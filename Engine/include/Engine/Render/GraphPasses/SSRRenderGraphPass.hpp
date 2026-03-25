#ifndef ELIX_SSR_RENDER_GRAPH_PASS_HPP
#define ELIX_SSR_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/RenderTarget.hpp"
#include "Engine/Skybox.hpp"
#include "Engine/Texture.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class SSRRenderGraphPass : public IRenderGraphPass
{
public:
    SSRRenderGraphPass(std::vector<RGPResourceHandler> &litColorHandlers,
                       std::vector<RGPResourceHandler> &normalHandlers,
                       RGPResourceHandler &depthHandler,
                       std::vector<RGPResourceHandler> &materialHandlers);

    void prepareRecord(const RenderGraphPassPerFrameData &data,
                       const RenderGraphPassContext &renderContext) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    bool isEnabled() const override;

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
    std::vector<RGPResourceHandler> &m_litColorHandlers;
    std::vector<RGPResourceHandler> &m_normalHandlers;
    RGPResourceHandler &m_depthHandler;
    std::vector<RGPResourceHandler> &m_materialHandlers;

    std::vector<RGPResourceHandler> m_outputHandlers;
    std::vector<const RenderTarget *> m_outputTargets;

    VkFormat m_format{VK_FORMAT_R16G16B16A16_SFLOAT};

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_sampler{nullptr};
    core::Sampler::SharedPtr m_depthSampler{nullptr};
    Texture::SharedPtr m_fallbackEnvironmentTexture{nullptr};
    std::unique_ptr<Skybox> m_environmentSkybox{nullptr};
    std::string m_requestedSkyboxHDRPath;
    std::string m_loadedSkyboxHDRPath;
    bool m_pendingSkyboxUpdate{true};

    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D m_scissor{};

    void ensureFallbackEnvironmentTexture();
    void updateEnvironmentSkybox();
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SSR_RENDER_GRAPH_PASS_HPP
