#include "Engine/Render/GraphPasses/BloomRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/PushConstant.hpp"

#include <glm/glm.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

struct BloomExtractPC
{
    glm::vec2 texelSize;
    float     threshold;
    float     knee;
    float     enabled;
    float     pad[3];
};

BloomRenderGraphPass::BloomRenderGraphPass(std::vector<RGPResourceHandler> &hdrInputHandlers)
    : m_hdrInputHandlers(hdrInputHandlers)
{
    setDebugName("Bloom extract render graph pass");
    m_clearValues[0].color = {0.f, 0.f, 0.f, 1.f};
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void BloomRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    for (auto &h : m_hdrInputHandlers)
        builder.read(h, RGPTextureUsage::SAMPLED);

    // Half-resolution output
    RGPTextureDescription bloomDesc{m_bloomFormat, RGPTextureUsage::COLOR_ATTACHMENT};
    bloomDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    bloomDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    bloomDesc.setCustomExtentFunction([this] { return m_bloomExtent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        bloomDesc.setDebugName("__ELIX_BLOOM_EXTRACT_" + std::to_string(i) + "__");
        auto h = builder.createTexture(bloomDesc);
        m_bloomHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT);
    }

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(device, std::vector<VkDescriptorSetLayoutBinding>{binding});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<BloomExtractPC>::getRange(VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void BloomRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_bloomRenderTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_bloomRenderTargets[i] = storage.getTexture(m_bloomHandlers[i]);
        auto hdrTarget          = storage.getTexture(m_hdrInputHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(hdrTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .build(core::VulkanContext::getContext()->getDevice(),
                                             core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                             m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(hdrTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void BloomRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                  const RenderGraphPassPerFrameData & /*data*/,
                                  const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    GraphicsPipelineKey key{};
    key.shader         = ShaderId::BloomExtract;
    key.cull           = CullMode::None;
    key.depthTest      = false;
    key.depthWrite     = false;
    key.depthCompare   = VK_COMPARE_OP_LESS;
    key.polygonMode    = VK_POLYGON_MODE_FILL;
    key.topology       = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats   = {m_bloomFormat};
    key.pipelineLayout = m_pipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                            0, 1, &m_descriptorSets[renderContext.currentImageIndex], 0, nullptr);

    const auto &settings = RenderQualitySettings::getInstance();
    BloomExtractPC pc{};
    pc.texelSize = {1.0f / m_extent.width, 1.0f / m_extent.height};
    pc.threshold = settings.bloomThreshold;
    pc.knee      = settings.bloomKnee;
    pc.enabled   = (settings.enablePostProcessing && settings.enableBloom) ? 1.0f : 0.0f;

    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<IRenderGraphPass::RenderPassExecution> BloomRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_bloomExtent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView   = m_bloomRenderTargets[renderContext.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue  = m_clearValues[0];

    exec.colorsRenderingItems = {color};
    exec.useDepth             = false;
    exec.colorFormats         = {m_bloomFormat};
    exec.depthFormat          = VK_FORMAT_UNDEFINED;

    exec.targets[m_bloomHandlers[renderContext.currentImageIndex]] =
        m_bloomRenderTargets[renderContext.currentImageIndex];

    return {exec};
}

void BloomRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent      = extent;
    m_bloomExtent = {std::max(1u, extent.width / 2), std::max(1u, extent.height / 2)};
    m_viewport    = {0.0f, 0.0f, (float)m_bloomExtent.width, (float)m_bloomExtent.height, 0.0f, 1.0f};
    m_scissor     = {{0, 0}, m_bloomExtent};
    requestRecompilation();
}

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END
