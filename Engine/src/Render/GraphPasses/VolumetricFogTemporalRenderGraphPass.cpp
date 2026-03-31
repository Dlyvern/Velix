#include "Engine/Render/GraphPasses/VolumetricFogTemporalRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/VulkanHelpers.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
    VkExtent2D computeFogExtent(VkExtent2D fullExtent)
    {
        const auto quality = RenderQualitySettings::getInstance().volumetricFogQuality;
        const float scale = quality == RenderQualitySettings::VolumetricFogQuality::High ? 0.5f : 0.25f;

        return {
            std::max(1u, static_cast<uint32_t>(std::max(1.0f, std::floor(static_cast<float>(fullExtent.width) * scale)))),
            std::max(1u, static_cast<uint32_t>(std::max(1.0f, std::floor(static_cast<float>(fullExtent.height) * scale))))};
    }

    struct FogTemporalPC
    {
        glm::mat4 prevViewProj;
        glm::vec4 params0;
    };

    glm::vec3 extractTranslation(const glm::mat4 &matrix)
    {
        return glm::vec3(matrix[3]);
    }

    glm::vec3 extractForward(const glm::mat4 &view)
    {
        const glm::mat4 invView = glm::inverse(view);
        return glm::normalize(-glm::vec3(invView[2]));
    }
} // namespace

VolumetricFogTemporalRenderGraphPass::VolumetricFogTemporalRenderGraphPass(
    std::vector<RGPResourceHandler> &inputHandlers,
    RGPResourceHandler &depthHandler)
    : m_inputHandlers(inputHandlers), m_depthHandler(depthHandler)
{
    setDebugName("Volumetric fog temporal render graph pass");
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 0.0f};
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    outputs.color.setOwner(this);
}

void VolumetricFogTemporalRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_outputHandlers.clear();

    builder.read(m_depthHandler, RGPTextureUsage::SAMPLED);
    for (auto &handler : m_inputHandlers)
        builder.read(handler, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this] { return m_extent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_VOLUMETRIC_FOG_TEMPORAL_" + std::to_string(i) + "__");
        auto handler = builder.createTexture(outDesc);
        m_outputHandlers.push_back(handler);
        builder.write(handler, RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC);
    }
    outputs.color.set(m_outputHandlers);

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding currentFogBinding{};
    currentFogBinding.binding = 0;
    currentFogBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    currentFogBinding.descriptorCount = 1;
    currentFogBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding historyBinding{};
    historyBinding.binding = 1;
    historyBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    historyBinding.descriptorCount = 1;
    historyBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding depthBinding{};
    depthBinding.binding = 2;
    depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthBinding.descriptorCount = 1;
    depthBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(
        device,
        std::vector<VkDescriptorSetLayoutBinding>{currentFogBinding, historyBinding, depthBinding});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
            *EngineShaderFamilies::cameraDescriptorSetLayout,
            *m_descriptorSetLayout},
        std::vector<VkPushConstantRange>{
            PushConstant<FogTemporalPC>::getRange(VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_sampler = core::Sampler::createShared(
        VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_depthSampler = core::Sampler::createShared(
        VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void VolumetricFogTemporalRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    auto device = core::VulkanContext::getContext()->getDevice();

    m_outputTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    m_historyTarget = std::make_shared<RenderTarget>(
        device->vk(),
        m_extent,
        m_format,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    m_historyTarget->createVkImageView();

    {
        auto commandBuffer = core::CommandBuffer::create(*core::VulkanContext::getContext()->getGraphicsCommandPool());
        commandBuffer.begin();

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

        VkDependencyInfo dependencyInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

        VkClearColorValue clearColor{};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(commandBuffer, m_historyTarget->getImage()->vk(),
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

        commandBuffer.end();
        commandBuffer.submit(core::VulkanContext::getContext()->getGraphicsQueue());
        {
            std::lock_guard<std::mutex> lock(core::helpers::queueHostSyncMutex());
            vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
        }
    }

    m_historyInitialized = false;
    m_hasPrevFrame = false;

    const auto *depthTexture = storage.getTexture(m_depthHandler);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        const auto *fogTexture = storage.getTexture(m_inputHandlers[i]);
        m_outputTargets[i] = storage.getTexture(m_outputHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(fogTexture->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .addImage(m_historyTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                                      .addImage(depthTexture->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 2)
                                      .build(core::VulkanContext::getContext()->getDevice(),
                                             core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                             m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(fogTexture->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(m_historyTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .addImage(depthTexture->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 2)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void VolumetricFogTemporalRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                                  const RenderGraphPassPerFrameData &data,
                                                  const RenderGraphPassContext &renderContext)
{
    const uint32_t imageIndex = renderContext.currentImageIndex;

    if (renderContext.executionIndex == 0)
    {
        vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
        vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

        GraphicsPipelineKey key{};
        key.shader = ShaderId::VolumetricFogTemporal;
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

        VkDescriptorSet sets[2] = {
            data.cameraDescriptorSet,
            m_descriptorSets[imageIndex]};

        vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout, 0, 2, sets, 0, nullptr);

        const glm::mat4 currentInvView = glm::inverse(data.view);
        const glm::vec3 currentCameraPos = extractTranslation(currentInvView);
        const glm::vec3 currentForward = extractForward(data.view);

        bool historyValid = m_historyInitialized && m_hasPrevFrame && (data.fogSettingsHash == m_prevFogSettingsHash);
        if (historyValid)
        {
            const glm::mat4 prevInvView = glm::inverse(m_prevView);
            const glm::vec3 prevCameraPos = extractTranslation(prevInvView);
            const glm::vec3 prevForward = extractForward(m_prevView);
            const float translationDelta = glm::length(currentCameraPos - prevCameraPos);
            const float forwardDot = glm::dot(currentForward, prevForward);
            if (translationDelta > 1.5f || forwardDot < 0.9f)
                historyValid = false;
        }

        FogTemporalPC pc{};
        pc.prevViewProj = m_prevProjection * m_prevView;
        pc.params0 = glm::vec4(historyValid ? 0.9f : 0.0f, historyValid ? 1.0f : 0.0f, 0.0f, 0.0f);

        vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
        return;
    }

    VkImage outputImage = m_outputTargets[imageIndex]->getImage()->vk();
    VkImage historyImage = m_historyTarget->getImage()->vk();

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

    VkDependencyInfo preDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    preDependency.imageMemoryBarrierCount = 2;
    preDependency.pImageMemoryBarriers = preCopy;
    vkCmdPipelineBarrier2(commandBuffer->vk(), &preDependency);

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

    VkDependencyInfo postDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    postDependency.imageMemoryBarrierCount = 2;
    postDependency.pImageMemoryBarriers = postCopy;
    vkCmdPipelineBarrier2(commandBuffer->vk(), &postDependency);

    m_historyInitialized = true;
    m_hasPrevFrame = true;
    m_prevView = data.view;
    m_prevProjection = data.projection;
    m_prevFogSettingsHash = data.fogSettingsHash;
}

std::vector<IRenderGraphPass::RenderPassExecution>
VolumetricFogTemporalRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    const uint32_t imageIndex = renderContext.currentImageIndex;

    RenderPassExecution exec0{};
    exec0.mode = ExecutionMode::DynamicRendering;
    exec0.renderArea = {{0, 0}, m_extent};
    exec0.colorFormats = {m_format};
    exec0.depthFormat = VK_FORMAT_UNDEFINED;
    exec0.useDepth = false;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_outputTargets[imageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = m_clearValues[0];
    exec0.colorsRenderingItems = {color};
    exec0.targets[m_outputHandlers[imageIndex]] = m_outputTargets[imageIndex];

    RenderPassExecution exec1{};
    exec1.mode = ExecutionMode::Direct;
    exec1.renderArea = {{0, 0}, m_extent};

    return {exec0, exec1};
}

void VolumetricFogTemporalRenderGraphPass::setExtent(VkExtent2D extent)
{
    if (m_fullExtent.width == extent.width && m_fullExtent.height == extent.height)
        return;

    m_fullExtent = extent;
    updateInternalExtent();
    requestRecompilation();
}

void VolumetricFogTemporalRenderGraphPass::freeResources()
{
    m_outputHandlers.clear();
    m_outputTargets.clear();
    for (auto &descriptorSet : m_descriptorSets)
        descriptorSet = VK_NULL_HANDLE;
    m_descriptorSetsInitialized = false;
    outputs.color.set(MultiHandle{});
    m_historyTarget.reset();
    m_historyInitialized = false;
    m_hasPrevFrame = false;
    m_prevFogSettingsHash = 0u;
}

void VolumetricFogTemporalRenderGraphPass::updateInternalExtent()
{
    m_extent = computeFogExtent(m_fullExtent);
    m_viewport = {0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    m_scissor = {{0, 0}, m_extent};
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
