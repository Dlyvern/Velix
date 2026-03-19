#include "Engine/Render/GraphPasses/TAARenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/VulkanHelpers.hpp"

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
    struct TAAPc
    {
        glm::vec2 texelSize;
        float blendAlpha;
        float enabled;
    };
} // namespace

bool TAARenderGraphPass::isEnabled() const
{
    const auto &s = RenderQualitySettings::getInstance();
    return s.enablePostProcessing &&
           s.getAntiAliasingMode() == RenderQualitySettings::AntiAliasingMode::TAA;
}

TAARenderGraphPass::TAARenderGraphPass(std::vector<RGPResourceHandler> &inputHandlers)
    : m_inputHandlers(inputHandlers)
{
    setDebugName("TAA render graph pass");
    m_clearValues[0].color = {0.f, 0.f, 0.f, 1.f};
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void TAARenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_format = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();

    for (auto &h : m_inputHandlers)
        builder.read(h, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this]
                                    { return m_extent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_TAA_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC);
    }

    auto device = core::VulkanContext::getContext()->getDevice();

    std::vector<VkDescriptorSetLayoutBinding> bindings(2);
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(device, bindings);

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<TAAPc>::getRange(VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_sampler = core::Sampler::createShared(
        VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void TAARenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    auto device = core::VulkanContext::getContext()->getDevice();

    m_outputTargets.resize(imageCount);

    {
        m_historyTarget = std::make_shared<RenderTarget>(
            device->vk(),
            m_extent,
            m_format,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        m_historyTarget->createVkImageView();

        auto cmdBuf = core::CommandBuffer::create(*core::VulkanContext::getContext()->getGraphicsCommandPool());
        cmdBuf.begin();

        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_historyTarget->getImage()->vk();
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmdBuf, &dep);

        VkClearColorValue clearColor{};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmdBuf, m_historyTarget->getImage()->vk(),
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier2(cmdBuf, &dep);

        cmdBuf.end();
        cmdBuf.submit(core::VulkanContext::getContext()->getGraphicsQueue());
        {
            std::lock_guard<std::mutex> lock(core::helpers::queueHostSyncMutex());
            vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
        }
    }

    m_historyInitialized = false;

    m_descriptorSets.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputTargets[i] = storage.getTexture(m_outputHandlers[i]);
        auto inputTarget = storage.getTexture(m_inputHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(inputTarget->vkImageView(), m_sampler,
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .addImage(m_historyTarget->vkImageView(), m_sampler,
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                                      .build(device,
                                             core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                             m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(inputTarget->vkImageView(), m_sampler,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(m_historyTarget->vkImageView(), m_sampler,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .update(device, m_descriptorSets[i]);
        }
    }
    m_descriptorSetsInitialized = true;
}

void TAARenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                const RenderGraphPassPerFrameData & /*data*/,
                                const RenderGraphPassContext &renderContext)
{
    const auto &settings = RenderQualitySettings::getInstance();
    const bool taaActive = settings.enablePostProcessing &&
                           settings.getAntiAliasingMode() ==
                               RenderQualitySettings::AntiAliasingMode::TAA;
    const uint32_t idx = renderContext.currentImageIndex;

    if (renderContext.executionIndex == 0)
    {
        vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
        vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

        GraphicsPipelineKey key{};
        key.shader = ShaderId::TAA;
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
                                &m_descriptorSets[idx], 0, nullptr);

        TAAPc pc{};
        pc.texelSize = {m_extent.width > 0 ? 1.0f / static_cast<float>(m_extent.width) : 0.0f,
                        m_extent.height > 0 ? 1.0f / static_cast<float>(m_extent.height) : 0.0f};
        pc.blendAlpha = taaActive ? 0.1f : 1.0f;
        // Use passthrough when TAA is off OR when history isn't ready yet
        pc.enabled = (taaActive && m_historyInitialized) ? 1.0f : 0.0f;

        vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
        return;
    }

    auto *outputImage = m_outputTargets[idx]->getImage()->vk();
    auto *historyImage = m_historyTarget->getImage()->vk();

    VkImageMemoryBarrier2 preCopy[2]{};
    preCopy[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    preCopy[0].srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    preCopy[0].srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    preCopy[0].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    preCopy[0].dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    preCopy[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    preCopy[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    preCopy[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preCopy[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preCopy[0].image = outputImage;
    preCopy[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    preCopy[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    preCopy[1].srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    preCopy[1].srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    preCopy[1].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    preCopy[1].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    preCopy[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    preCopy[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    preCopy[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preCopy[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preCopy[1].image = historyImage;
    preCopy[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo depPre{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depPre.imageMemoryBarrierCount = 2;
    depPre.pImageMemoryBarriers = preCopy;
    vkCmdPipelineBarrier2(commandBuffer->vk(), &depPre);

    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {m_extent.width, m_extent.height, 1};
    vkCmdCopyImage(commandBuffer->vk(),
                   outputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   historyImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &region);

    VkImageMemoryBarrier2 postCopy[2]{};
    postCopy[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    postCopy[0].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    postCopy[0].srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    postCopy[0].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    postCopy[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    postCopy[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    postCopy[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    postCopy[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postCopy[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postCopy[0].image = outputImage;
    postCopy[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    postCopy[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    postCopy[1].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    postCopy[1].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    postCopy[1].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    postCopy[1].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    postCopy[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    postCopy[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    postCopy[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postCopy[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postCopy[1].image = historyImage;
    postCopy[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo depPost{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depPost.imageMemoryBarrierCount = 2;
    depPost.pImageMemoryBarriers = postCopy;
    vkCmdPipelineBarrier2(commandBuffer->vk(), &depPost);

    if (!m_historyInitialized)
        m_historyInitialized = true;
}

std::vector<IRenderGraphPass::RenderPassExecution>
TAARenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    const uint32_t idx = renderContext.currentImageIndex;

    RenderPassExecution exec0{};
    exec0.mode = ExecutionMode::DynamicRendering;
    exec0.renderArea = {{0, 0}, m_extent};
    exec0.colorFormats = {m_format};
    exec0.depthFormat = VK_FORMAT_UNDEFINED;
    exec0.useDepth = false;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_outputTargets[idx]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = m_clearValues[0];
    exec0.colorsRenderingItems = {color};

    exec0.targets[m_outputHandlers[idx]] = m_outputTargets[idx];

    RenderPassExecution exec1{};
    exec1.mode = ExecutionMode::Direct;
    exec1.renderArea = {{0, 0}, m_extent};

    return {exec0, exec1};
}

void TAARenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = {0.f, 0.f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.f, 1.f};
    m_scissor = {{0, 0}, extent};
    requestRecompilation();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
