#ifndef ELIX_BASE_RENDER_GRAPH_PASS_HPP
#define ELIX_BASE_RENDER_GRAPH_PASS_HPP

#include "Core/Macros.hpp"

#include "Core/CommandBuffer.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/CommandPool.hpp"
#include "Core/Buffer.hpp"
#include "Core/RenderPass.hpp"
#include "Core/SyncObject.hpp"
#include "Core/GraphicsPipeline.hpp"
#include "Core/DescriptorSetLayout.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/RenderPass.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/TextureImage.hpp"
#include "Engine/Scene.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/Hash.hpp"
#include "Engine/Material.hpp"
#include "Engine/Skybox.hpp"

#include <unordered_map>
#include <cstddef>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

//TODO make it better:
enum class SceneRenderMask : uint8_t
{
    OFFSCREEN = 0,
    SWAP_CHAIN = 1,
    BOTH = 2 //Maybe just use |
};

inline SceneRenderMask operator|(SceneRenderMask a, SceneRenderMask b) { return SceneRenderMask(unsigned(a) | unsigned(b)); }

class BaseRenderGraphPass : public IRenderGraphPass
{
public:
    BaseRenderGraphPass(VkDevice device, core::SwapChain::SharedPtr swapchain,
    VkDescriptorPool descriptorPool, SceneRenderMask usageMask = SceneRenderMask::OFFSCREEN | SceneRenderMask::SWAP_CHAIN);

    void update(const RenderGraphPassContext& renderData) override;
    void setup(RenderGraphPassRecourceBuilder& graphPassBuilder) override;
    void compile(RenderGraphPassResourceHash& storage) override;
    void execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData& data) override;

    void getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const override;

    std::vector<VkImageView> getImageViews() const
    {
        std::vector<VkImageView> imageViews;
        imageViews.reserve(m_offscreenColorImages.size());

        for(const auto& texture : m_offscreenColorImages)
            imageViews.push_back(texture->vkImageView());

        return imageViews;
    }

private:
    void setupOffscreenResources(RenderGraphPassRecourceBuilder& graphPassBuilder);
    void setupSwapChainResources(RenderGraphPassRecourceBuilder& graphPassBuilder);

    SceneRenderMask m_usageMask;

    std::array<VkClearValue, 2> m_clearValues;

    VkDevice m_device{VK_NULL_HANDLE};

    core::SwapChain::SharedPtr m_swapchain{nullptr};
    core::CommandPool::SharedPtr m_commandPool{nullptr};
    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};

    core::GraphicsPipeline::SharedPtr m_graphicsPipeline{nullptr};
    core::GraphicsPipeline::SharedPtr m_offscreenGraphicsPipeline{nullptr};

    std::vector<core::Framebuffer::SharedPtr> m_framebuffers;
    std::vector<core::Framebuffer::SharedPtr> m_offscreenFramebuffers;

    core::RenderPass::SharedPtr m_renderPass{nullptr};
    core::RenderPass::SharedPtr m_offscreenRenderPass{nullptr};

    std::vector<core::Texture::SharedPtr> m_offscreenColorImages;

    uint32_t m_imageIndex{0};
    uint32_t m_currentFrame{0};

    std::size_t m_depthImageHash;

    std::size_t m_renderPassHash;
    std::size_t m_offscreenRenderPassHash;

    std::size_t m_offscreenGraphicsPipelineHash;
    std::size_t m_graphicsPipelineHash;

    std::vector<std::size_t> m_offscreenFramebufferHashes;
    std::vector<std::size_t> m_framebufferHashes;

    std::vector<std::size_t> m_colorTextureHashes;

    core::Texture::SharedPtr m_depthImageTexture;

    VkDescriptorPool m_descriptorPool;

    std::unique_ptr<Skybox> m_skybox{nullptr};
    std::unique_ptr<Skybox> m_offscreenSkybox{nullptr};

};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_BASE_RENDER_GRAPH_PASS_HPP