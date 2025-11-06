#ifndef ELIX_OFFSCREEN_RENDER_GRAPH_PASS_HPP
#define ELIX_OFFSCREEN_RENDER_GRAPH_PASS_HPP

#include "Core/Macros.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/SwapChain.hpp"
#include "Core/GraphicsPipeline.hpp"
#include "Core/RenderPass.hpp"
#include "Core/CommandPool.hpp"
#include "Core/Framebuffer.hpp"
#include "Core/Texture.hpp"

#include "Engine/Skybox.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include <array>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class OffscreenRenderGraphPass : public IRenderGraphPass
{
public:
    OffscreenRenderGraphPass(VkDescriptorPool descriptorPool);
    void setup(RenderGraphPassRecourceBuilder& graphPassBuilder) override;
    void compile(RenderGraphPassResourceHash& storage) override;
    void execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData& data) override;
    void update(const RenderGraphPassContext& renderData) override;
    void getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const override;

    void beginRenderPass() override {}
    void endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer) override {}
    void cleanup() override {}

    std::vector<VkImageView> getImageViews() const
    {
        std::vector<VkImageView> imageViews;
        imageViews.reserve(m_colorImages.size());

        for(const auto& texture : m_colorImages)
            imageViews.push_back(texture->vkImageView());

        return imageViews;
    }
private:
    std::array<VkClearValue, 2> m_clearValues;
    uint32_t m_imageIndex{0};
    uint32_t m_currentFrame{0};
    std::weak_ptr<core::PipelineLayout> m_pipelineLayout;
    std::weak_ptr<core::SwapChain> m_swapChain;
    core::GraphicsPipeline::SharedPtr m_graphicsPipeline{nullptr};
    core::RenderPass::SharedPtr m_renderPass{nullptr};
    core::CommandPool::SharedPtr m_commandPool{nullptr};
    VkDevice m_device{VK_NULL_HANDLE};
    std::size_t m_depthImageHash;
    std::size_t m_graphicsPipelineHash;
    std::size_t m_renderPassHash;

    std::vector<std::size_t> m_framebufferHashes;
    std::vector<std::size_t> m_colorTextureHashes;

    std::vector<core::Texture::SharedPtr> m_colorImages;
    core::Texture::SharedPtr m_depthImageTexture;
    std::vector<core::Framebuffer::SharedPtr> m_framebuffers;
    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};

    std::unique_ptr<Skybox> m_skybox{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_OFFSCREEN_RENDER_GRAPH_PASS_HPP