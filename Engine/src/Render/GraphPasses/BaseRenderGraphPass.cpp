#include "Engine/Render/GraphPasses/BaseRenderGraphPass.hpp"

#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"

#include "Core/VulkanHelpers.hpp"
#include <iostream>

struct ModelPushConstant
{
    glm::mat4 model;
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)

BaseRenderGraphPass::BaseRenderGraphPass(VkDevice device, core::SwapChain::SharedPtr swapchain, uint32_t maxFrameInFlight,
core::GraphicsPipeline::SharedPtr graphicsPipeline, core::PipelineLayout::SharedPtr pipelineLayout, const std::vector<VkDescriptorSet>& descriptorSets,
const std::vector<VkDescriptorSet>& lightDescriptorSets) : 
m_device(device), m_swapchain(swapchain), m_graphicsPipeline(graphicsPipeline), m_pipelineLayout(pipelineLayout),
m_descriptorSet(descriptorSets), m_lightDescriptorSet(lightDescriptorSets)
{
    auto queueFamilyIndices = core::VulkanContext::findQueueFamilies(core::VulkanContext::getContext()->getPhysicalDevice(), core::VulkanContext::getContext()->getSurface());
    m_commandPool = core::CommandPool::create(device, queueFamilyIndices.graphicsFamily.value());
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    m_clearValues[1].depthStencil = {1.0f, 0};
}

void BaseRenderGraphPass::getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const
{
    renderPassBeginInfo = VkRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass = m_swapChainProxy->renderPassProxy->storage.data->vk();
    renderPassBeginInfo.framebuffer = m_swapChainProxy->storage.data[m_imageIndex];
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = m_swapchain->getExtent();
    renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(m_clearValues.size());
    renderPassBeginInfo.pClearValues = m_clearValues.data();
}

void BaseRenderGraphPass::setup(RenderGraphPassBuilder::SharedPtr builder)
{
    m_staticMeshProxy = builder->createProxy<StaticMeshRenderGraphProxy>("__ELIX_SCENE_STATIC_MESH_PROXY__");
    m_swapChainProxy = builder->createProxy<SwapChainRenderGraphProxy>("__ELIX_SWAP_CHAIN_PROXY__");
}

void BaseRenderGraphPass::compile()
{
}

void BaseRenderGraphPass::update(uint32_t currentFrame, uint32_t currentImageIndex, VkFramebuffer fr)
{
    m_imageIndex = currentImageIndex;
    m_currentFrame = currentFrame;
    m_currentFramebuffer = fr;
}

void BaseRenderGraphPass::execute(core::CommandBuffer::SharedPtr commandBuffer)
{
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline->vk());

    auto viewport = m_swapchain->getViewport();
    auto scissor = m_swapchain->getScissor();
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &scissor);

    for(const auto [enity, mesh] : m_staticMeshProxy->transformationBasedOnMesh)
    {
        VkBuffer vertexBuffers[] = {mesh->vertexBuffer->vkBuffer()};

        VkDeviceSize offset[] = {0};
        vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);
        vkCmdBindIndexBuffer(commandBuffer->vk(), mesh->indexBuffer->vkBuffer(), 0, mesh->indexType);

        // getDevice()->gpuProps.limits.maxPushConstantsSize;

        ModelPushConstant modelPushConstant;
        modelPushConstant.model = glm::mat4(1.0f);

        if(auto tr = enity->getComponent<Transform3DComponent>())
            modelPushConstant.model = tr->getMatrix();
        else
            std::cerr << "ERROR: MODEL DOES NOT HAVE TRANSFORM COMPONENT. SOMETHING IS WEIRD" << std::endl;
        
        vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ModelPushConstant), &modelPushConstant);

        VkDescriptorSet materialDst = Material::getDefaultMaterial()->getDescriptorSet(m_currentFrame);

        if(auto staticMesh = enity->getComponent<StaticMeshComponent>())
            if(auto material = staticMesh->getMaterial())
                materialDst = material->getDescriptorSet(m_currentFrame);


        std::array<VkDescriptorSet, 3> descriptorSets = 
        {
            m_descriptorSet[m_currentFrame],           // set 0: camera
            materialDst,                               // set 1: material
            m_lightDescriptorSet[m_currentFrame]      // set 2: lighting
        };

        vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout->vk(), 0, static_cast<uint32_t>(descriptorSets.size()), 
        descriptorSets.data(), 0, nullptr);

        // vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout->vk(), 1, 1, &materialDst, 0, nullptr);

        // vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout->vk(), 2, 1, &m_lightDescriptorSet[m_currentFrame], 0, nullptr);
        // vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout->vk(), 0, 1, &m_descriptorSet[m_currentFrame], 0, nullptr);

        vkCmdDrawIndexed(commandBuffer->vk(), mesh->indicesCount, 1, 0, 0, 0);
    }
}

ELIX_NESTED_NAMESPACE_END