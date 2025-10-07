#ifndef ELIX_BASE_RENDER_GRAPH_PASS_HPP
#define ELIX_BASE_RENDER_GRAPH_PASS_HPP

#include "Core/Macros.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include "Core/CommandBuffer.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/CommandPool.hpp"
#include "Core/Buffer.hpp"
#include "Core/RenderPass.hpp"
#include "Core/SyncObject.hpp"
#include "Core/GraphicsPipeline.hpp"
#include "Core/DescriptorSetLayout.hpp"
#include "Core/PipelineLayout.hpp"
#include "Engine/TextureImage.hpp"
#include "Engine/Scene.hpp"

#include "Engine/Render/Proxies/ImageRenderGraphProxy.hpp"
#include "Engine/Render/Proxies/SwapChainRenderGraphProxy.hpp"

#include "Engine/Mesh.hpp"
#include "Engine/Hash.hpp"
#include "Engine/Material.hpp"

#include "Engine/Render/Proxies/StaticMeshRenderGraphProxy.hpp"

#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class BaseRenderGraphPass : public IRenderGraphPass
{
public:
    BaseRenderGraphPass(VkDevice device, core::SwapChain::SharedPtr swapchain, uint32_t maxFrameInFlight,
    core::GraphicsPipeline::SharedPtr graphicsPipeline, core::PipelineLayout::SharedPtr pipelineLayout,
    const std::vector<VkDescriptorSet>& descriptorSets, const std::vector<VkDescriptorSet>& lightDescriptorSets);
    void update(uint32_t currentFrame, uint32_t currentImageIndex, VkFramebuffer fr);
    void setup(std::shared_ptr<RenderGraphPassBuilder> builder) override;
    void compile() override;
    void execute(core::CommandBuffer::SharedPtr commandBuffer) override;

    void  getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const override;
private:
    std::array<VkClearValue, 2> m_clearValues;

    ImageRenderGraphProxy::SharedPtr m_swapChainImagesProxy{nullptr};

    SwapChainRenderGraphProxy::SharedPtr m_swapChainProxy{nullptr};

    VkDevice m_device{VK_NULL_HANDLE};

    core::SwapChain::SharedPtr m_swapchain{nullptr};
    core::CommandPool::SharedPtr m_commandPool{nullptr};
    core::GraphicsPipeline::SharedPtr m_graphicsPipeline{nullptr};
    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};

    VkFramebuffer m_currentFramebuffer{VK_NULL_HANDLE};

    std::vector<VkDescriptorSet> m_descriptorSet;
    std::vector<VkDescriptorSet> m_lightDescriptorSet;
    
    uint32_t m_imageIndex{0};
    uint32_t m_currentFrame{0};

    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};

    StaticMeshRenderGraphProxy::SharedPtr m_staticMeshProxy{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_BASE_RENDER_GRAPH_PASS_HPP