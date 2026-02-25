#ifndef IMGUI_RENDER_GRAPH_PASS_HPP
#define IMGUI_RENDER_GRAPH_PASS_HPP

#include "Core/Sampler.hpp"
#include "Core/DescriptorPool.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Editor/Editor.hpp"

#include <array>
#include <vector>
#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class ImGuiRenderGraphPass : public engine::renderGraph::IRenderGraphPass
{
public:
    using SharedPtr = std::shared_ptr<ImGuiRenderGraphPass>;

    //! Maybe this is not a good idea
    ImGuiRenderGraphPass(std::shared_ptr<Editor> editor, std::vector<engine::renderGraph::RGPResourceHandler> &offscreenTexture,
                         engine::renderGraph::RGPResourceHandler &objectIdTextureHandler);

    void setup(engine::renderGraph::RGPResourcesBuilder &builder) override;
    void compile(engine::renderGraph::RGPResourcesStorage &storage) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassPerFrameData &data,
                const engine::RenderGraphPassContext &renderContext) override;
    std::vector<RenderPassExecution> getRenderPassExecutions(const engine::RenderGraphPassContext &renderContext) const override;

    void setViewportImages(const std::vector<VkImageView> &imageViews);

    void cleanup();

private:
    void initImGui();

    core::Sampler::SharedPtr m_sampler{nullptr};

    VkDevice m_device{VK_NULL_HANDLE};
    std::array<VkClearValue, 1> m_clearValues;
    std::shared_ptr<Editor> m_editor{nullptr};

    std::vector<const engine::RenderTarget *> m_colorRenderTargets;

    VkFormat m_colorFormat;

    std::vector<VkDescriptorSet> m_descriptorSets;

    engine::renderGraph::RGPResourceHandler m_colorTextureHandler;

    std::vector<engine::renderGraph::RGPResourceHandler> &m_offscreenTextureHandler;
    engine::renderGraph::RGPResourceHandler &m_objectIdTextureHandler;

    core::DescriptorPool::SharedPtr m_imguiDescriptorPool{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif // IMGUI_RENDER_GRAPH_PASS_HPPs
