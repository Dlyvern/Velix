#ifndef ELIX_OFFSCREEN_RENDER_GRAPH_PASS_HPP
#define ELIX_OFFSCREEN_RENDER_GRAPH_PASS_HPP

#include "Core/Macros.hpp"

#include "Core/Image.hpp"
#include "Core/RenderPass.hpp"
#include "Core/CommandPool.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/GraphicsPipeline.hpp"
#include "Core/DescriptorSetLayout.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/Proxies/ImageRenderGraphProxy.hpp"
#include "Engine/Render/Proxies/StaticMeshRenderGraphProxy.hpp"
#include "Engine/Render/Proxies/RenderPassRenderGraphProxy.hpp"

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class OffscreenRenderGraphPass : public IRenderGraphPass
{
public:
    OffscreenRenderGraphPass(core::PipelineLayout::SharedPtr pipelineLayout, const std::vector<VkDescriptorSet>& descriptorSets,
    core::GraphicsPipeline::SharedPtr graphicsPipeline);
    void setup(std::shared_ptr<RenderGraphPassBuilder> builder) override;
    void compile() override;
    void execute(core::CommandBuffer::SharedPtr commandBuffer) override;
    void update(uint32_t currentFrame, uint32_t currentImageIndex, VkFramebuffer fr) override;
    void getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const override;
private:
    void createImages();
    void createImageViews();
    void createFramebuffers();

    std::array<VkClearValue, 2> m_clearValue;
    std::vector<core::Image::SharedPtr> m_images;
    std::vector<VkImageView> m_imageViews;

    ImageRenderGraphProxy::SharedPtr m_depthImageProxy{nullptr};

    uint32_t m_currentFrame;
    uint32_t m_imageIndex;
    std::vector<VkDescriptorSet> m_descriptorSet;

    // RenderPassRenderGraphProxy::SharedPtr m_viewportRenderPassProxy{nullptr};
    core::RenderPass::SharedPtr m_renderPass{nullptr};

    StaticMeshRenderGraphProxy::SharedPtr m_staticMeshProxy{nullptr};
    core::CommandPool::SharedPtr m_commandPool{nullptr};
    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::GraphicsPipeline::SharedPtr m_graphicsPipeline{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};

    std::vector<VkFramebuffer> m_framebuffers{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_OFFSCREEN_RENDER_GRAPH_PASS_HPP