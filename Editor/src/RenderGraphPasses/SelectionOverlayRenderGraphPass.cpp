#include "Editor/RenderGraphPasses/SelectionOverlayRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"

#include <glm/glm.hpp>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

namespace
{
    struct SelectionOverlayPC
    {
        uint32_t selectedEntityId;
        uint32_t selectedMeshSlot;
        float outlineMix;
        float padding0;
        glm::vec4 outlineColor;
    };
} // namespace

SelectionOverlayRenderGraphPass::SelectionOverlayRenderGraphPass(std::shared_ptr<Editor> editor,
                                                                 std::vector<engine::renderGraph::RGPResourceHandler> &sceneColorHandlers,
                                                                 engine::renderGraph::RGPResourceHandler &objectIdHandler)
    : m_editor(std::move(editor)),
      m_sceneColorHandlers(sceneColorHandlers),
      m_objectIdHandler(objectIdHandler)
{
    setDebugName("Selection overlay render graph pass");

    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};

    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void SelectionOverlayRenderGraphPass::setup(engine::renderGraph::RGPResourcesBuilder &builder)
{
    m_colorFormat = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();

    for (const auto &sceneColor : m_sceneColorHandlers)
        builder.read(sceneColor, engine::renderGraph::RGPTextureUsage::SAMPLED);

    builder.read(m_objectIdHandler, engine::renderGraph::RGPTextureUsage::SAMPLED);

    engine::renderGraph::RGPTextureDescription colorTextureDesc{m_colorFormat, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT};
    colorTextureDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    colorTextureDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    colorTextureDesc.setCustomExtentFunction([this]
                                             { return m_extent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_colorTextureHandlers.clear();
    m_colorTextureHandlers.reserve(imageCount);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        colorTextureDesc.setDebugName("__ELIX_SELECTION_OVERLAY_" + std::to_string(i) + "__");
        auto h = builder.createTexture(colorTextureDesc);
        m_colorTextureHandlers.push_back(h);
        builder.write(h, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
    }

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding sceneBinding{};
    sceneBinding.binding = 0;
    sceneBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sceneBinding.descriptorCount = 1;
    sceneBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    sceneBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding objectIdBinding{};
    objectIdBinding.binding = 1;
    objectIdBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    objectIdBinding.descriptorCount = 1;
    objectIdBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    objectIdBinding.pImmutableSamplers = nullptr;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(device, std::vector<VkDescriptorSetLayoutBinding>{sceneBinding, objectIdBinding});
    m_pipelineLayout = core::PipelineLayout::createShared(device,
                                                          std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout},
                                                          std::vector<VkPushConstantRange>{engine::PushConstant<SelectionOverlayPC>::getRange(VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_colorSampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_objectIdSampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void SelectionOverlayRenderGraphPass::compile(engine::renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_colorRenderTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    auto objectIdTexture = storage.getTexture(m_objectIdHandler);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_colorRenderTargets[i] = storage.getTexture(m_colorTextureHandlers[i]);

        auto sceneColorTexture = storage.getTexture(m_sceneColorHandlers[i]);

        if (!m_descriptorSetsBuilt || m_descriptorSets[i] == VK_NULL_HANDLE)
        {
            m_descriptorSets[i] = engine::DescriptorSetBuilder::begin()
                                      .addImage(sceneColorTexture->vkImageView(), m_colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .addImage(objectIdTexture->vkImageView(), m_objectIdSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                                      .build(core::VulkanContext::getContext()->getDevice(), core::VulkanContext::getContext()->getPersistentDescriptorPool(), m_descriptorSetLayout);
        }
        else
        {
            engine::DescriptorSetBuilder::begin()
                .addImage(sceneColorTexture->vkImageView(), m_colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(objectIdTexture->vkImageView(), m_objectIdSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    m_descriptorSetsBuilt = true;
}

void SelectionOverlayRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassPerFrameData &data,
                                             const engine::RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    engine::GraphicsPipelineKey key{};
    key.shader = engine::ShaderId::SelectionOverlay;
    key.cull = engine::CullMode::None;
    key.depthTest = false;
    key.depthWrite = false;
    key.depthCompare = VK_COMPARE_OP_LESS;
    key.polygonMode = VK_POLYGON_MODE_FILL;
    key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats = {m_colorFormat};
    key.pipelineLayout = m_pipelineLayout;

    auto graphicsPipeline = engine::GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[renderContext.currentImageIndex], 0, nullptr);

    SelectionOverlayPC pc{};
    pc.selectedEntityId = m_editor ? m_editor->getSelectedEntityIdForBuffer() : 0u;
    pc.selectedMeshSlot = m_editor ? m_editor->getSelectedMeshSlotForBuffer() : 0u;
    pc.outlineMix = 0.85f;
    pc.outlineColor = glm::vec4(1.0f, 0.62f, 0.10f, 1.0f);

    vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SelectionOverlayPC), &pc);

    engine::renderGraph::profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<engine::renderGraph::IRenderGraphPass::RenderPassExecution> SelectionOverlayRenderGraphPass::getRenderPassExecutions(const engine::RenderGraphPassContext &renderContext) const
{
    engine::renderGraph::IRenderGraphPass::RenderPassExecution execution{};
    execution.renderArea.offset = {0, 0};
    execution.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_colorRenderTargets[renderContext.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = m_clearValues[0];

    execution.colorsRenderingItems = {color};
    execution.useDepth = false;
    execution.colorFormats = {m_colorFormat};
    execution.depthFormat = VK_FORMAT_UNDEFINED;

    execution.targets[m_colorTextureHandlers[renderContext.currentImageIndex]] =
        m_colorRenderTargets[renderContext.currentImageIndex];

    return {execution};
}

void SelectionOverlayRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = {0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    m_scissor = {{0, 0}, extent};
    requestRecompilation();
}

ELIX_NESTED_NAMESPACE_END
