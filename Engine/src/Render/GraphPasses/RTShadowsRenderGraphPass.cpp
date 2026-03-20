#include "Engine/Render/GraphPasses/RTShadowsRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
    VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
    {
        if (alignment == 0)
            return value;

        return (value + alignment - 1) & ~(alignment - 1);
    }

    VkDeviceAddress getBufferDeviceAddress(const core::Buffer &buffer)
    {
        VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        addressInfo.buffer = buffer.vk();
        return vkGetBufferDeviceAddress(core::VulkanContext::getContext()->getDevice(), &addressInfo);
    }
}

RTShadowsRenderGraphPass::RTShadowsRenderGraphPass(std::vector<RGPResourceHandler> &normalHandlers,
                                                   RGPResourceHandler &depthHandler)
    : m_normalHandlers(normalHandlers),
      m_depthHandler(depthHandler)
{
    setDebugName("RT Shadows render graph pass");
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    outputs.color.setOwner(this);
}

bool RTShadowsRenderGraphPass::canUsePipelinePath() const
{
    auto context = core::VulkanContext::getContext();
    return context &&
           context->hasRayTracingPipelineSupport() &&
           m_rayTracingPipeline != VK_NULL_HANDLE &&
           m_shaderBindingTable != nullptr;
}

bool RTShadowsRenderGraphPass::shouldUsePipelinePath() const
{
    const auto &settings = RenderQualitySettings::getInstance();
    return settings.enableRayTracing &&
           settings.enableRTShadows &&
           settings.rayTracingMode == RenderQualitySettings::RayTracingMode::Pipeline &&
           canUsePipelinePath();
}

bool RTShadowsRenderGraphPass::shouldUseRayQueryPath() const
{
    const auto &settings = RenderQualitySettings::getInstance();
    auto context = core::VulkanContext::getContext();
    return settings.enableRayTracing &&
           settings.enableRTShadows &&
           settings.rayTracingMode == RenderQualitySettings::RayTracingMode::RayQuery &&
           context &&
           context->hasRayQuerySupport() &&
           m_computePipeline != VK_NULL_HANDLE;
}

void RTShadowsRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    for (uint32_t i = 0; i < imageCount; ++i)
        builder.read(m_normalHandlers[i], RGPTextureUsage::SAMPLED);
    builder.read(m_depthHandler, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outputDescription{m_shadowFormat, RGPTextureUsage::COLOR_ATTACHMENT_STORAGE};
    outputDescription.setInitialLayout(VK_IMAGE_LAYOUT_GENERAL);
    outputDescription.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outputDescription.setArrayLayers(kMaxShadowLights);
    outputDescription.setImageViewtype(VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    outputDescription.setCustomExtentFunction([this]
                                              { return m_extent; });

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outputDescription.setDebugName("__ELIX_RT_SHADOWS_" + std::to_string(i) + "__");
        auto handler = builder.createTexture(outputDescription);
        m_outputHandlers.push_back(handler);
        builder.write(handler, RGPTextureUsage::COLOR_ATTACHMENT_STORAGE);
    }
    outputs.color.set(m_outputHandlers);

    auto device = core::VulkanContext::getContext()->getDevice();

    auto makeBinding = [](uint32_t binding, VkDescriptorType type) -> VkDescriptorSetLayoutBinding
    {
        VkDescriptorSetLayoutBinding descriptorSetLayoutBinding{};
        descriptorSetLayoutBinding.binding = binding;
        descriptorSetLayoutBinding.descriptorType = type;
        descriptorSetLayoutBinding.descriptorCount = 1;
        descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT;
        return descriptorSetLayoutBinding;
    };

    m_textureSetLayout = core::DescriptorSetLayout::createShared(
        device,
        std::vector<VkDescriptorSetLayoutBinding>{
            makeBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            makeBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            makeBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
            *EngineShaderFamilies::cameraDescriptorSetLayout,
            *m_textureSetLayout},
        std::vector<VkPushConstantRange>{
            PushConstant<RTShadowsPC>::getRange(VK_SHADER_STAGE_RAYGEN_BIT_KHR)});

    // Separate compute pipeline layout (push constants declared with COMPUTE stage)
    {
        VkPushConstantRange computePCRange = PushConstant<RTShadowsPC>::getRange(VK_SHADER_STAGE_COMPUTE_BIT);
        std::vector<VkDescriptorSetLayout> setLayouts = {
            EngineShaderFamilies::cameraDescriptorSetLayout->vk(),
            m_textureSetLayout->vk()};
        VkPipelineLayoutCreateInfo layoutCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutCI.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutCI.pSetLayouts = setLayouts.data();
        layoutCI.pushConstantRangeCount = 1;
        layoutCI.pPushConstantRanges = &computePCRange;
        vkCreatePipelineLayout(device, &layoutCI, nullptr, &m_computePipelineLayout);
    }

    m_sampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_depthSampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    if (core::VulkanContext::getContext()->hasRayTracingPipelineSupport())
        createRayTracingPipeline();

    if (core::VulkanContext::getContext()->hasRayQuerySupport())
        createComputePipeline();
}

void RTShadowsRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputRenderTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    const auto *depthTarget = storage.getTexture(m_depthHandler);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        const auto *normalTarget = storage.getTexture(m_normalHandlers[i]);
        const auto *outputTarget = storage.getTexture(m_outputHandlers[i]);

        m_outputRenderTargets[i] = outputTarget;

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(normalTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .addImage(depthTarget->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                                      .addStorageImage(outputTarget->vkImageView(), VK_IMAGE_LAYOUT_GENERAL, 2)
                                      .build(core::VulkanContext::getContext()->getDevice(),
                                             core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                             m_textureSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(normalTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(depthTarget->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                .addStorageImage(outputTarget->vkImageView(), VK_IMAGE_LAYOUT_GENERAL, 2)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void RTShadowsRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                      const RenderGraphPassPerFrameData &data,
                                      const RenderGraphPassContext &renderContext)
{
    const auto *outputTarget = m_outputRenderTargets[renderContext.currentImageIndex];
    if (!outputTarget)
        return;

    const bool rtShadowsEnabled = shouldUsePipelinePath() || shouldUseRayQueryPath();
    if (!rtShadowsEnabled)
    {
        VkClearColorValue clearColor{};
        VkImageSubresourceRange clearRange{};
        clearRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        clearRange.baseMipLevel   = 0;
        clearRange.levelCount     = 1;
        clearRange.baseArrayLayer = 0;
        clearRange.layerCount     = kMaxShadowLights;
        vkCmdClearColorImage(commandBuffer->vk(), outputTarget->getImage()->vk(),
                             VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &clearRange);
        return;
    }

    // No geometry → no shadows. Clear to 0 (unlit shadow mask = no shadow) and skip the
    // expensive RT/compute dispatch to avoid wasting GPU time on an empty TLAS.
    if (data.drawBatches.empty())
    {
        VkClearColorValue clearColor{};
        VkImageSubresourceRange clearRange{};
        clearRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        clearRange.baseMipLevel   = 0;
        clearRange.levelCount     = 1;
        clearRange.baseArrayLayer = 0;
        clearRange.layerCount     = kMaxShadowLights;
        vkCmdClearColorImage(commandBuffer->vk(), outputTarget->getImage()->vk(),
                             VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &clearRange);
        return;
    }

    RTShadowsPC pushConstants{};
    pushConstants.enableRTShadows = 1.0f;
    pushConstants.rtShadowSamples = static_cast<float>(std::clamp(RenderQualitySettings::getInstance().rtShadowSamples, 1, 16));
    pushConstants.rtShadowPenumbraSize = std::clamp(RenderQualitySettings::getInstance().rtShadowPenumbraSize, 0.0f, 2.0f);
    pushConstants.activeRTShadowLayerCount = static_cast<float>(std::min(renderContext.activeRTShadowLayerCount, kMaxShadowLights));

    VkDescriptorSet descriptorSets[2] = {
        data.cameraDescriptorSet,
        m_descriptorSets[renderContext.currentImageIndex]};

    if (shouldUsePipelinePath())
    {
        vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rayTracingPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 0, 2, descriptorSets, 0, nullptr);
        vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(pushConstants), &pushConstants);
        vkCmdTraceRaysKHR(commandBuffer->vk(), &m_raygenRegion, &m_missRegion, &m_hitRegion, &m_callableRegion, m_extent.width, m_extent.height, 1);
    }
    else if (shouldUseRayQueryPath())
    {
        vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
        vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipelineLayout, 0, 2, descriptorSets, 0, nullptr);
        vkCmdPushConstants(commandBuffer->vk(), m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);
        uint32_t gx = (m_extent.width + 7) / 8;
        uint32_t gy = (m_extent.height + 7) / 8;
        vkCmdDispatch(commandBuffer->vk(), gx, gy, 1);
    }
}

std::vector<IRenderGraphPass::RenderPassExecution> RTShadowsRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution execution{};
    execution.mode = IRenderGraphPass::ExecutionMode::Direct;
    execution.useDepth = false;
    execution.depthFormat = VK_FORMAT_UNDEFINED;
    execution.renderArea.offset = {0, 0};
    execution.renderArea.extent = m_extent;
    execution.targets[m_outputHandlers[renderContext.currentImageIndex]] = m_outputRenderTargets[renderContext.currentImageIndex];
    return {execution};
}

void RTShadowsRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    requestRecompilation();
}

void RTShadowsRenderGraphPass::cleanup()
{
    destroyRayTracingPipeline();
    destroyComputePipeline();

    // Destroy the compute pipeline layout (created once in setup(), valid for the pass lifetime)
    auto context = core::VulkanContext::getContext();
    if (context && m_computePipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(context->getDevice(), m_computePipelineLayout, nullptr);
        m_computePipelineLayout = VK_NULL_HANDLE;
    }
}

void RTShadowsRenderGraphPass::createRayTracingPipeline()
{
    destroyRayTracingPipeline();

    auto context = core::VulkanContext::getContext();
    if (!context || !context->hasRayTracingPipelineSupport())
        return;

    m_raygenShader.loadFromFile("./resources/shaders/rt_shadows.rgen.spv", core::ShaderStage::RAYGEN);
    m_missShader.loadFromFile("./resources/shaders/rt_shadows.rmiss.spv", core::ShaderStage::MISS);
    m_closestHitShader.loadFromFile("./resources/shaders/rt_shadows.rchit.spv", core::ShaderStage::CLOSEST_HIT);

    const std::array<VkPipelineShaderStageCreateInfo, 3> shaderStages = {
        m_raygenShader.getInfo(),
        m_missShader.getInfo(),
        m_closestHitShader.getInfo()};

    std::array<VkRayTracingShaderGroupCreateInfoKHR, 3> shaderGroups{};

    shaderGroups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroups[0].generalShader = 0;
    shaderGroups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

    shaderGroups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroups[1].generalShader = 1;
    shaderGroups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

    shaderGroups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    shaderGroups[2].generalShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[2].closestHitShader = 2;
    shaderGroups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

    VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCreateInfo.pStages = shaderStages.data();
    pipelineCreateInfo.groupCount = static_cast<uint32_t>(shaderGroups.size());
    pipelineCreateInfo.pGroups = shaderGroups.data();
    pipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
    pipelineCreateInfo.layout = m_pipelineLayout;

    if (vkCreateRayTracingPipelinesKHR(context->getDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_rayTracingPipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create RT shadows pipeline");

    createShaderBindingTable(static_cast<uint32_t>(shaderGroups.size()));
}

void RTShadowsRenderGraphPass::createShaderBindingTable(uint32_t groupCount)
{
    auto context = core::VulkanContext::getContext();
    const auto &rtProperties = context->getRayTracingPipelineProperties();

    const uint32_t handleSize = rtProperties.shaderGroupHandleSize;
    const uint32_t handleAlignment = rtProperties.shaderGroupHandleAlignment;
    const uint32_t baseAlignment = rtProperties.shaderGroupBaseAlignment;
    const VkDeviceSize handleSizeAligned = alignUp(handleSize, handleAlignment);

    m_raygenRegion = {};
    m_missRegion = {};
    m_hitRegion = {};
    m_callableRegion = {};

    m_raygenRegion.stride = alignUp(handleSizeAligned, baseAlignment);
    m_raygenRegion.size = m_raygenRegion.stride;

    m_missRegion.stride = handleSizeAligned;
    m_missRegion.size = alignUp(handleSizeAligned, baseAlignment);

    m_hitRegion.stride = handleSizeAligned;
    m_hitRegion.size = alignUp(handleSizeAligned, baseAlignment);

    const VkDeviceSize sbtSize = m_raygenRegion.size + m_missRegion.size + m_hitRegion.size;
    m_shaderBindingTable = core::Buffer::createShared(
        sbtSize,
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        core::memory::MemoryUsage::CPU_TO_GPU);

    std::vector<uint8_t> shaderHandleStorage(static_cast<size_t>(groupCount) * handleSize);
    if (vkGetRayTracingShaderGroupHandlesKHR(context->getDevice(),
                                             m_rayTracingPipeline,
                                             0,
                                             groupCount,
                                             shaderHandleStorage.size(),
                                             shaderHandleStorage.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to get RT shadows shader group handles");
    }

    void *mappedData = nullptr;
    m_shaderBindingTable->map(mappedData);

    auto *dst = static_cast<uint8_t *>(mappedData);
    const VkDeviceSize missOffset = m_raygenRegion.size;
    const VkDeviceSize hitOffset = missOffset + m_missRegion.size;

    std::memcpy(dst, shaderHandleStorage.data(), handleSize);
    std::memcpy(dst + missOffset, shaderHandleStorage.data() + handleSize, handleSize);
    std::memcpy(dst + hitOffset, shaderHandleStorage.data() + handleSize * 2u, handleSize);

    m_shaderBindingTable->unmap();

    const VkDeviceAddress baseAddress = getBufferDeviceAddress(*m_shaderBindingTable);
    m_raygenRegion.deviceAddress = baseAddress;
    m_missRegion.deviceAddress = baseAddress + missOffset;
    m_hitRegion.deviceAddress = baseAddress + hitOffset;
}

void RTShadowsRenderGraphPass::destroyRayTracingPipeline()
{
    auto context = core::VulkanContext::getContext();
    if (context && m_rayTracingPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(context->getDevice(), m_rayTracingPipeline, nullptr);
        m_rayTracingPipeline = VK_NULL_HANDLE;
    }

    m_callableRegion = {};
    m_hitRegion = {};
    m_missRegion = {};
    m_raygenRegion = {};
    m_shaderBindingTable.reset();

    m_closestHitShader.destroyVk();
    m_missShader.destroyVk();
    m_raygenShader.destroyVk();
}

void RTShadowsRenderGraphPass::createComputePipeline()
{
    destroyComputePipeline();

    auto context = core::VulkanContext::getContext();
    if (!context || !context->hasRayQuerySupport())
        return;

    m_computeShader.loadFromFile("./resources/shaders/rt_shadows_rq.comp.spv", core::ShaderStage::COMPUTE);

    VkComputePipelineCreateInfo pipelineCI{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineCI.stage = m_computeShader.getInfo();
    pipelineCI.layout = m_computePipelineLayout;

    if (vkCreateComputePipelines(context->getDevice(), VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_computePipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create RT shadows compute pipeline");
}

void RTShadowsRenderGraphPass::destroyComputePipeline()
{
    auto context = core::VulkanContext::getContext();
    if (context && m_computePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(context->getDevice(), m_computePipeline, nullptr);
        m_computePipeline = VK_NULL_HANDLE;
    }

    m_computeShader.destroyVk();
}


void RTShadowsRenderGraphPass::freeResources()
{
    m_outputRenderTargets.clear();
    for (auto &s : m_descriptorSets) s = VK_NULL_HANDLE;
    m_descriptorSetsInitialized = false;
    outputs.color.set(MultiHandle{});
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
