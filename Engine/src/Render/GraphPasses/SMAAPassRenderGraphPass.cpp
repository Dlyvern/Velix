#include "Engine/Render/GraphPasses/SMAAPassRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/PushConstant.hpp"

#include <glm/glm.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
    struct SMAApc
    {
        glm::vec2 texelSize;
        float enabled;
        float edgeThreshold;  // default: 0.1
        float maxSearchSteps; // default: 16
        float _pad0;
        float _pad1;
        float _pad2;
    };
}

SMAAPassRenderGraphPass::SMAAPassRenderGraphPass(std::vector<RGPResourceHandler> &inputHandlers)
    : m_inputHandlers(inputHandlers)
{
    setDebugName("SMAA render graph pass");
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void SMAAPassRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_format = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();

    for (auto &h : m_inputHandlers)
        builder.read(h, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this]
                                    { return m_extent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_SMAA_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT);
    }

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding inputBinding{};
    inputBinding.binding = 0;
    inputBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    inputBinding.descriptorCount = 1;
    inputBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{inputBinding});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<SMAApc>::getRange(VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                            VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void SMAAPassRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputTargets[i] = storage.getTexture(m_outputHandlers[i]);

        auto inputTarget = storage.getTexture(m_inputHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(inputTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .build(core::VulkanContext::getContext()->getDevice(),
                                             core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                             m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(inputTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void SMAAPassRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                     const RenderGraphPassPerFrameData & /*data*/,
                                     const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    GraphicsPipelineKey key{};
    key.shader = ShaderId::SMAA;
    key.cull = CullMode::None;
    key.depthTest = false;
    key.depthWrite = false;
    key.depthCompare = VK_COMPARE_OP_LESS;
    key.polygonMode = VK_POLYGON_MODE_FILL;
    key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats = {m_format};
    key.pipelineLayout = m_pipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1,
                            &m_descriptorSets[renderContext.currentImageIndex], 0, nullptr);

    const auto &settings = RenderQualitySettings::getInstance();
    SMAApc pc{};
    pc.texelSize = {1.0f / m_extent.width, 1.0f / m_extent.height};
    pc.enabled = (settings.enableSMAA && settings.enablePostProcessing) ? 1.0f : 0.0f;
    pc.edgeThreshold = 0.1f;
    pc.maxSearchSteps = 16.0f;

    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<IRenderGraphPass::RenderPassExecution>
SMAAPassRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_outputTargets[renderContext.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = m_clearValues[0];

    exec.colorsRenderingItems = {color};
    exec.useDepth = false;
    exec.colorFormats = {m_format};
    exec.depthFormat = VK_FORMAT_UNDEFINED;

    exec.targets[m_outputHandlers[renderContext.currentImageIndex]] =
        m_outputTargets[renderContext.currentImageIndex];

    return {exec};
}

void SMAAPassRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    m_scissor = {{0, 0}, extent};
    requestRecompilation();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
