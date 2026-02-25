#ifndef ELIX_SELECTION_OVERLAY_RENDER_GRAPH_PASS_HPP
#define ELIX_SELECTION_OVERLAY_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/DescriptorSetLayout.hpp"
#include "Core/Sampler.hpp"

#include "Editor/Editor.hpp"

#include <array>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class SelectionOverlayRenderGraphPass : public engine::renderGraph::IRenderGraphPass
{
public:
    SelectionOverlayRenderGraphPass(std::shared_ptr<Editor> editor,
                                    uint32_t scenePassId,
                                    uint32_t objectIdPassId,
                                    std::vector<engine::renderGraph::RGPResourceHandler> &sceneColorHandlers,
                                    engine::renderGraph::RGPResourceHandler &objectIdHandler);

    void setup(engine::renderGraph::RGPResourcesBuilder &builder) override;
    void compile(engine::renderGraph::RGPResourcesStorage &storage) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassPerFrameData &data,
                const engine::RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const engine::RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    std::vector<engine::renderGraph::RGPResourceHandler> &getHandlers()
    {
        return m_colorTextureHandlers;
    }

private:
    std::shared_ptr<Editor> m_editor{nullptr};

    std::array<VkClearValue, 1> m_clearValues;

    std::vector<engine::renderGraph::RGPResourceHandler> &m_sceneColorHandlers;
    engine::renderGraph::RGPResourceHandler &m_objectIdHandler;
    std::vector<engine::renderGraph::RGPResourceHandler> m_colorTextureHandlers;

    std::vector<const engine::RenderTarget *> m_colorRenderTargets;

    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};
    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    std::vector<VkDescriptorSet> m_descriptorSets;
    bool m_descriptorSetsBuilt{false};

    VkFormat m_colorFormat{VK_FORMAT_UNDEFINED};
    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D m_scissor{};

    core::Sampler::SharedPtr m_colorSampler{nullptr};
    core::Sampler::SharedPtr m_objectIdSampler{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SELECTION_OVERLAY_RENDER_GRAPH_PASS_HPP
