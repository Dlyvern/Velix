#ifndef ELIX_DECAL_RENDER_GRAPH_PASS_HPP
#define ELIX_DECAL_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/RenderTarget.hpp"
#include "Engine/Scene.hpp"

#include "Core/Buffer.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"
#include "Core/DescriptorSetLayout.hpp"

#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class DecalRenderGraphPass : public IRenderGraphPass
{
public:
    DecalRenderGraphPass(std::vector<RGPResourceHandler> &albedoHandlers,
                         std::vector<RGPResourceHandler> &normalHandlers,
                         std::vector<RGPResourceHandler> &materialHandlers,
                         std::vector<RGPResourceHandler> &emissiveHandlers,
                         RGPResourceHandler              &depthHandler);

    void setScene(Scene *scene) { m_scene = scene; }

    void setup(renderGraph::RGPResourcesBuilder &builder) override;
    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    bool isEnabled() const override;
    void cleanup() override;
    void freeResources() override;

private:
    std::vector<RGPResourceHandler> &m_albedoHandlers;
    std::vector<RGPResourceHandler> &m_normalHandlers;
    std::vector<RGPResourceHandler> &m_materialHandlers;
    std::vector<RGPResourceHandler> &m_emissiveHandlers;
    RGPResourceHandler              &m_depthHandler;

    std::vector<const RenderTarget *> m_albedoTargets;
    std::vector<const RenderTarget *> m_normalTargets;
    std::vector<const RenderTarget *> m_materialTargets;
    std::vector<const RenderTarget *> m_emissiveTargets;
    const RenderTarget               *m_depthTarget{nullptr};

    core::PipelineLayout::SharedPtr     m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_depthSetLayout{nullptr};

    std::vector<VkDescriptorSet> m_depthDescriptorSets;
    bool m_descriptorSetsInitialized{false};

    core::Sampler::SharedPtr m_depthSampler{nullptr};

    core::Buffer::SharedPtr m_cubeVB{nullptr};
    core::Buffer::SharedPtr m_cubeIB{nullptr};

    VkExtent2D m_extent{};

    Scene *m_scene{nullptr};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_DECAL_RENDER_GRAPH_PASS_HPP
