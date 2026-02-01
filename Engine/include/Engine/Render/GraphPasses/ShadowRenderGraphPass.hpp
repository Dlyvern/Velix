#ifndef ELIX_SHADOW_RENDER_GRAPH_PASS_HPP
#define ELIX_SHADOW_RENDER_GRAPH_PASS_HPP

#include "Core/Macros.hpp"
#include "Core/RenderPass.hpp"
#include "Core/Image.hpp"
#include "Core/GraphicsPipeline.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/DescriptorSetLayout.hpp"
#include "Core/CommandPool.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/Framebuffer.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include <cstdint>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class ShadowRenderGraphPass : public IRenderGraphPass
{
public:
    ShadowRenderGraphPass();
    void execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data) override;
    void update(const RenderGraphPassContext &renderData) override;
    void getRenderPassBeginInfo(VkRenderPassBeginInfo &renderPassBeginInfo) const override;
    void endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer) override;

    void setup(renderGraph::RGPResourcesBuilder &builder) override;
    void compile(renderGraph::RGPResourcesStorage &storage) override;

    VkSampler getSampler()
    {
        return m_sampler;
    }

    VkImageView getImageView()
    {
        return m_renderTarget->vkImageView();
    }

    RGPResourceHandler &getShadowHandler()
    {
        return m_depthTextureHandler;
    }

private:
    core::RenderPass::SharedPtr m_renderPass{nullptr};
    core::GraphicsPipeline::SharedPtr m_graphicsPipeline{nullptr};
    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};
    VkSampler m_sampler{VK_NULL_HANDLE};

    const RenderTarget *m_renderTarget{nullptr};

    core::Framebuffer::SharedPtr m_framebuffer{nullptr};

    VkViewport m_viewport{};
    VkRect2D m_scissor{};

    uint32_t m_width{4096};
    uint32_t m_height{4096};

    VkClearValue m_clearValue;

    RGPResourceHandler m_depthTextureHandler;
};
ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SHADOW_RENDER_GRAPH_PASS_HPP