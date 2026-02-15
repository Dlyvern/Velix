#ifndef ELIX_PREVIEW_ASSETS_RENDER_GRAPH_PASS_HPP
#define ELIX_PREVIEW_ASSETS_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include <vector>
#include <array>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class PreviewAssetsRenderGraphPass : public engine::renderGraph::IRenderGraphPass
{
public:
    struct PreviewJob
    {
        engine::Material *material;
        // GPUMesh *mesh;
        VkImageView output;
    };

    PreviewAssetsRenderGraphPass(VkExtent2D extent);

    int addMaterialPreviewJob(const PreviewJob &previewJob);

    VkImageView getVkImageView(int index)
    {
        return m_renderTargets[index]->vkImageView();
    }

    void setup(engine::renderGraph::RGPResourcesBuilder &builder) override;
    void compile(engine::renderGraph::RGPResourcesStorage &storage) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassPerFrameData &data,
                const engine::RenderGraphPassContext &renderContext) override;

    void endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassContext &context) override;
    void startBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassContext &context) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const engine::RenderGraphPassContext &renderContext) const override;

    std::vector<VkImageView> getRenderedImages() const
    {
        std::vector<VkImageView> result;

        result.reserve(m_indexBusyJobs);

        for (uint32_t i = 0; i < m_indexBusyJobs; ++i)
            result.push_back(m_renderTargets[i]->vkImageView());

        return result;
    }

    static constexpr uint8_t MAX_RENDER_JOBS = UINT8_MAX;

    void clearJobs();

private:
    std::array<PreviewJob, MAX_RENDER_JOBS> m_renderJobs;
    std::array<const engine::RenderTarget *, MAX_RENDER_JOBS> m_renderTargets;
    std::array<engine::renderGraph::RGPResourceHandler, MAX_RENDER_JOBS> m_resourceHandlers;

    VkViewport m_viewport;
    VkRect2D m_scissor;
    VkExtent2D m_extent;
    std::array<VkClearValue, 1> m_clearValues;

    engine::GPUMesh::SharedPtr m_circleGpuMesh{nullptr};

    uint32_t m_indexBusyJobs{0};
    uint32_t m_currentJob{0};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PREVIEW_ASSETS_RENDER_GRAPH_PASS_HPP