#ifndef IMGUI_RENDER_GRAPH_PASS_HPP
#define IMGUI_RENDER_GRAPH_PASS_HPP

#include "Core/Framebuffer.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Editor/Editor.hpp"

#include <array>
#include <vector>
#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class ImGuiRenderGraphPass : public engine::IRenderGraphPass
{
public:
    using SharedPtr = std::shared_ptr<ImGuiRenderGraphPass>;

    //!Maybe this is not a good idea
    ImGuiRenderGraphPass(std::shared_ptr<Editor> editor);

    void setup(engine::RenderGraphPassRecourceBuilder& graphPassBuilder) override;
    void compile(engine::RenderGraphPassResourceHash& storage) override;
    void execute(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassPerFrameData& data) override;

    void update(const engine::RenderGraphPassContext& renderData) override;

    void getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const override;

    void setViewportImages(const std::vector<VkImageView>& imageViews);
private:
    VkDevice m_device{VK_NULL_HANDLE};
    std::array<VkClearValue, 2> m_clearValues;
    std::shared_ptr<Editor> m_editor{nullptr};

    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
    VkSampler m_sampler;

    core::RenderPass::SharedPtr m_renderPass{nullptr};

    uint32_t m_currentImageIndex;
    uint32_t m_currentFrame;

    std::vector<VkDescriptorSet> m_descriptorSets;
    std::vector<std::size_t> m_framebufferHashes;
    std::vector<core::Framebuffer::SharedPtr> m_framebuffers;
};

ELIX_NESTED_NAMESPACE_END

#endif //IMGUI_RENDER_GRAPH_PASS_HPPs