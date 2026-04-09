#include "Editor/RenderGraphPasses/ObjectIdResolveRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

ObjectIdResolveRenderGraphPass::ObjectIdResolveRenderGraphPass(
    engine::renderGraph::RGPResourceHandler &msaaObjectIdHandler,
    engine::renderGraph::RGPResourceHandler &resolvedObjectIdHandler)
    : m_msaaObjectIdHandler(msaaObjectIdHandler),
      m_resolvedObjectIdHandler(resolvedObjectIdHandler)
{
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 0.0f};
    setDebugName("Object ID resolve render graph pass");
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void ObjectIdResolveRenderGraphPass::setup(engine::renderGraph::RGPResourcesBuilder &builder)
{
    builder.read(m_msaaObjectIdHandler, engine::renderGraph::RGPTextureUsage::SAMPLED);
    builder.write(m_resolvedObjectIdHandler, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC);

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding objectIdBinding{};
    objectIdBinding.binding = 0;
    objectIdBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    objectIdBinding.descriptorCount = 1;
    objectIdBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    objectIdBinding.pImmutableSamplers = nullptr;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(
        device,
        std::vector<VkDescriptorSetLayoutBinding>{objectIdBinding});
    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout});
    m_sampler = core::Sampler::createShared(
        VK_FILTER_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void ObjectIdResolveRenderGraphPass::compile(engine::renderGraph::RGPResourcesStorage &storage)
{
    m_msaaObjectIdRenderTarget = storage.getTexture(m_msaaObjectIdHandler);
    m_resolvedObjectIdRenderTarget = storage.getTexture(m_resolvedObjectIdHandler);

    if (!m_msaaObjectIdRenderTarget || !m_resolvedObjectIdRenderTarget)
        return;

    if (!m_descriptorSetBuilt || m_descriptorSet == VK_NULL_HANDLE)
    {
        m_descriptorSet = engine::DescriptorSetBuilder::begin()
                              .addImage(m_msaaObjectIdRenderTarget->vkImageView(),
                                        m_sampler,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                        0)
                              .build(core::VulkanContext::getContext()->getDevice(),
                                     core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                     m_descriptorSetLayout);
    }
    else
    {
        engine::DescriptorSetBuilder::begin()
            .addImage(m_msaaObjectIdRenderTarget->vkImageView(),
                      m_sampler,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      0)
            .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSet);
    }

    m_descriptorSetBuilt = true;
}

void ObjectIdResolveRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                            const engine::RenderGraphPassPerFrameData &,
                                            const engine::RenderGraphPassContext &)
{
    if (m_descriptorSet == VK_NULL_HANDLE)
        return;

    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    engine::GraphicsPipelineKey key{};
    key.shader = engine::ShaderId::ObjectIdResolve;
    key.blend = engine::BlendMode::None;
    key.cull = engine::CullMode::None;
    key.depthTest = false;
    key.depthWrite = false;
    key.depthCompare = VK_COMPARE_OP_ALWAYS;
    key.polygonMode = VK_POLYGON_MODE_FILL;
    key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats = {m_colorFormat};
    key.pipelineLayout = m_pipelineLayout;

    auto graphicsPipeline = engine::GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            0,
                            1,
                            &m_descriptorSet,
                            0,
                            nullptr);

    engine::renderGraph::profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

void ObjectIdResolveRenderGraphPass::freeResources()
{
    m_msaaObjectIdRenderTarget = nullptr;
    m_resolvedObjectIdRenderTarget = nullptr;
    m_descriptorSet = VK_NULL_HANDLE;
    m_descriptorSetBuilt = false;
}

std::vector<engine::renderGraph::IRenderGraphPass::RenderPassExecution>
ObjectIdResolveRenderGraphPass::getRenderPassExecutions(const engine::RenderGraphPassContext &) const
{
    engine::renderGraph::IRenderGraphPass::RenderPassExecution execution{};
    execution.renderArea.offset = {0, 0};
    execution.renderArea.extent = m_extent;
    execution.useDepth = false;
    execution.colorFormats = {m_colorFormat};
    execution.depthFormat = VK_FORMAT_UNDEFINED;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_resolvedObjectIdRenderTarget ? m_resolvedObjectIdRenderTarget->vkImageView() : VK_NULL_HANDLE;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = m_clearValues[0];

    execution.colorsRenderingItems = {color};
    if (m_resolvedObjectIdRenderTarget)
        execution.targets[m_resolvedObjectIdHandler] = m_resolvedObjectIdRenderTarget;

    return {execution};
}

void ObjectIdResolveRenderGraphPass::setExtent(VkExtent2D extent)
{
    if (m_extent.width == extent.width && m_extent.height == extent.height)
        return;

    m_extent = extent;
    m_viewport = {0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    m_scissor = {{0, 0}, extent};
    requestRecompilation();
}

ELIX_NESTED_NAMESPACE_END
