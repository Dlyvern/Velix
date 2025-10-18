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


#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include <cstdint>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ShadowRenderGraphPass : public IRenderGraphPass
{
public:
    ShadowRenderGraphPass(VkDevice device);
    void setup(RenderGraphPassRecourceBuilder& graphPassBuilder);
    void compile(RenderGraphPassResourceHash& storage);
    void execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData& data);
    void update(const RenderGraphPassContext& renderData);
    void getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const;
    void endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer) override;

    VkSampler getSampler()
    {
        return m_sampler;
    }

    VkImageView getImageView()
    {
        return m_depthImageView;
    }

private:
    core::CommandPool::SharedPtr m_commandPool{nullptr};

    core::RenderPass::SharedPtr m_renderPass{nullptr};
    core::Image<core::ImageDeleter>::SharedPtr m_depthImage{nullptr};
    core::GraphicsPipeline::SharedPtr m_graphicsPipeline{nullptr};
    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};
    VkSampler m_sampler{VK_NULL_HANDLE};
    VkImageView m_depthImageView{VK_NULL_HANDLE};
    VkFramebuffer m_framebuffer{VK_NULL_HANDLE};

    uint32_t m_width{2048};
    uint32_t m_height{2048};

    VkClearValue m_clearValue;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_SHADOW_RENDER_GRAPH_PASS_HPP