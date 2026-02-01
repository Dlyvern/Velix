#ifndef ELIX_OFFSCREEN_RENDER_GRAPH_PASS_HPP
#define ELIX_OFFSCREEN_RENDER_GRAPH_PASS_HPP

#include "Core/Macros.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/SwapChain.hpp"
#include "Core/GraphicsPipeline.hpp"
#include "Core/RenderPass.hpp"
#include "Core/CommandPool.hpp"
#include "Core/Framebuffer.hpp"
#include "Engine/Render/RenderTarget.hpp"
#include "Core/Buffer.hpp"
#include "Engine/Skybox.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include <array>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class OffscreenRenderGraphPass : public IRenderGraphPass
{
public:
    OffscreenRenderGraphPass(VkDescriptorPool descriptorPool);
    void execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data) override;
    void update(const RenderGraphPassContext &renderData) override;
    void getRenderPassBeginInfo(VkRenderPassBeginInfo &renderPassBeginInfo) const override;

    void createSkeleton(VkSampler sampler, VkImageView imageView, int maxFramesInFlight);

    void beginRenderPass() override {}
    void endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer) override {}
    void cleanup() override {}

    void setViewport(VkViewport viewport);
    void setScissor(VkRect2D scissor);

    void onSwapChainResized(renderGraph::RGPResourcesStorage &storage) override;
    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    std::vector<VkImageView> getImageViews() const
    {
        std::vector<VkImageView> imageViews;
        imageViews.reserve(m_colorImages.size());

        for (const auto &texture : m_colorImages)
            imageViews.push_back(texture->vkImageView());

        return imageViews;
    }

private:
    std::array<VkClearValue, 2> m_clearValues;
    uint32_t m_imageIndex{0};
    uint32_t m_currentFrame{0};
    std::weak_ptr<core::SwapChain> m_swapChain;
    core::GraphicsPipeline::SharedPtr m_graphicsPipeline{nullptr};
    core::GraphicsPipeline::SharedPtr m_skeletalGraphicsPipeline{nullptr};
    core::GraphicsPipeline::SharedPtr m_wireframeGraphicsPipeline{nullptr};
    core::GraphicsPipeline::SharedPtr m_stencilGraphicsPipeline{nullptr};
    core::RenderPass::SharedPtr m_renderPass{nullptr};
    VkDevice m_device{VK_NULL_HANDLE};

    std::vector<std::size_t> m_framebufferHashes;
    std::vector<std::size_t> m_colorTextureHashes;

    std::vector<const RenderTarget *> m_colorImages;
    std::vector<core::Framebuffer::SharedPtr> m_framebuffers;
    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};

    std::unique_ptr<Skybox> m_skybox{nullptr};

    VkViewport m_viewport{};
    VkRect2D m_scissor{};

    std::vector<void *> m_cameraMapped;
    std::vector<core::Buffer::SharedPtr> m_cameraUniformObjects;

    std::vector<void *> m_cameraWireframeMapped;
    std::vector<core::Buffer::SharedPtr> m_cameraWireframeUniformObjects;
    std::vector<VkDescriptorSet> m_cameraWireframeDescriptorSets;

    std::vector<VkDescriptorSet> m_cameraDescriptorSets;

    std::vector<void *> m_lightMapped;
    std::vector<core::Buffer::SharedPtr> m_lightSpaceMatrixUniformObjects;

    std::vector<core::Buffer::SharedPtr> m_bonesSSBOs;

    std::vector<RGPResourceHandler> m_colorTextureHandler;
    RGPResourceHandler m_depthTextureHandler;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_OFFSCREEN_RENDER_GRAPH_PASS_HPP