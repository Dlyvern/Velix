#ifndef IMGUI_RENDER_GRAPH_PASS_HPP
#define IMGUI_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/Proxies/SwapChainRenderGraphProxy.hpp"

#include "Editor/Editor.hpp"

#include <array>
#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class ImGuiRenderGraphPass : public engine::IRenderGraphPass
{
public:
    using SharedPtr = std::shared_ptr<ImGuiRenderGraphPass>;

    //!Maybe this is not a good idea
    ImGuiRenderGraphPass(std::shared_ptr<Editor> editor);

    void setup(std::shared_ptr<engine::RenderGraphPassBuilder> builder) override;
    void compile() override;
    void execute(core::CommandBuffer::SharedPtr commandBuffer) override;

    void update(uint32_t currentFrame, uint32_t currentImageIndex, VkFramebuffer fr) override;

    void getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const override;



private:
    std::array<VkClearValue, 2> m_clearValues;
    void createFramebuffers();
    VkFramebuffer m_currentFramebuffer{VK_NULL_HANDLE};
    std::shared_ptr<Editor> m_editor{nullptr};

    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};

    std::vector<VkFramebuffer> m_framebuffers;
    core::RenderPass::SharedPtr m_renderPass{nullptr};

    uint32_t m_currentImageIndex;

    engine::SwapChainRenderGraphProxy::SharedPtr m_swapChainProxy{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //IMGUI_RENDER_GRAPH_PASS_HPPs