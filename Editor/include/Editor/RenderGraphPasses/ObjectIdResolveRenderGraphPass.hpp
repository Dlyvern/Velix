#ifndef ELIX_OBJECT_ID_RESOLVE_RENDER_GRAPH_PASS_HPP
#define ELIX_OBJECT_ID_RESOLVE_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Core/DescriptorSetLayout.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class ObjectIdResolveRenderGraphPass : public engine::renderGraph::IRenderGraphPass
{
public:
    ObjectIdResolveRenderGraphPass(engine::renderGraph::RGPResourceHandler &msaaObjectIdHandler,
                                   engine::renderGraph::RGPResourceHandler &resolvedObjectIdHandler);

    void setup(engine::renderGraph::RGPResourcesBuilder &builder) override;
    void compile(engine::renderGraph::RGPResourcesStorage &storage) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer,
                const engine::RenderGraphPassPerFrameData &data,
                const engine::RenderGraphPassContext &renderContext) override;
    void freeResources() override;

    std::vector<RenderPassExecution> getRenderPassExecutions(
        const engine::RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

private:
    std::array<VkClearValue, 1> m_clearValues{};

    engine::renderGraph::RGPResourceHandler &m_msaaObjectIdHandler;
    engine::renderGraph::RGPResourceHandler &m_resolvedObjectIdHandler;

    const engine::RenderTarget *m_msaaObjectIdRenderTarget{nullptr};
    const engine::RenderTarget *m_resolvedObjectIdRenderTarget{nullptr};

    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};
    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::Sampler::SharedPtr m_sampler{nullptr};
    VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};
    bool m_descriptorSetBuilt{false};

    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D m_scissor{};
    VkFormat m_colorFormat{VK_FORMAT_R32_UINT};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_OBJECT_ID_RESOLVE_RENDER_GRAPH_PASS_HPP
