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

#include <unordered_map>
#include <cstddef>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class BaseRenderGraphPass : public IRenderGraphPass
{
public:
    BaseRenderGraphPass(VkDevice device, core::SwapChain::SharedPtr swapchain, core::GraphicsPipeline::SharedPtr graphicsPipeline,
    core::PipelineLayout::SharedPtr pipelineLayout);

    void update(const RenderGraphPassContext& renderData) override;
    void setup(RenderGraphPassRecourceBuilder& graphPassBuilder) override;
    void compile(RenderGraphPassResourceHash& storage) override;
    void execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData& data) override;

    void getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const override;
private:
    std::array<VkClearValue, 2> m_clearValues;

    VkDevice m_device{VK_NULL_HANDLE};

    core::SwapChain::SharedPtr m_swapchain{nullptr};
    core::CommandPool::SharedPtr m_commandPool{nullptr};
    core::GraphicsPipeline::SharedPtr m_graphicsPipeline{nullptr};
    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};

    uint32_t m_imageIndex{0};
    uint32_t m_currentFrame{0};

    std::size_t m_depthImageHash;
    std::size_t m_renderPassHash;

    std::vector<std::size_t> m_framebufferHashes;

    core::Texture<core::ImageNoDelete>::SharedPtr m_depthImageTexture;

    std::vector<core::Framebuffer::SharedPtr> m_framebuffers;
    core::RenderPass::SharedPtr m_renderPass{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_BASE_RENDER_GRAPH_PASS_HPP