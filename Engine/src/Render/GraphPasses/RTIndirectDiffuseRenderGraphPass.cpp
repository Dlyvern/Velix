#include "Engine/Render/GraphPasses/RTIndirectDiffuseRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Render/RenderGraphPassPerFrameData.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include <algorithm>
#include <stdexcept>

namespace
{
    constexpr uint32_t    kMinGISceneBufferCount = 2u;
    constexpr VkDeviceSize kMinGISceneBufferSize  = 16u;
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

RTIndirectDiffuseRenderGraphPass::RTIndirectDiffuseRenderGraphPass(
    std::vector<RGPResourceHandler> &normalHandlers,
    std::vector<RGPResourceHandler> &albedoHandlers,
    RGPResourceHandler              &depthHandler)
    : m_normalHandlers(normalHandlers),
      m_albedoHandlers(albedoHandlers),
      m_depthHandler(depthHandler)
{
    setDebugName("RT Indirect Diffuse render graph pass");
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    outputs.color.setOwner(this);
}

bool RTIndirectDiffuseRenderGraphPass::canUsePipelinePath() const
{
    auto context = core::VulkanContext::getContext();
    return context &&
           context->hasRayTracingPipelineSupport() &&
           m_rayTracingPipeline &&
           m_rayTracingPipeline->isValid() &&
           m_shaderBindingTable &&
           m_shaderBindingTable->isValid();
}

void RTIndirectDiffuseRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        builder.read(m_normalHandlers[i], RGPTextureUsage::SAMPLED);
        builder.read(m_albedoHandlers[i], RGPTextureUsage::SAMPLED);
    }
    builder.read(m_depthHandler, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT_STORAGE};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_GENERAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this] { return m_extent; });

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_RT_GI_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT_STORAGE);
    }
    outputs.color.set(m_outputHandlers);

    auto device = core::VulkanContext::getContext()->getDevice();

    auto makeBinding = [](uint32_t binding, VkDescriptorType type, VkShaderStageFlags stages) -> VkDescriptorSetLayoutBinding
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding         = binding;
        b.descriptorType  = type;
        b.descriptorCount = 1;
        b.stageFlags      = stages;
        return b;
    };

    const VkShaderStageFlags rgenStages = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    const VkShaderStageFlags chitStages = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    // Set 1: b0=normal, b1=albedo, b2=depth, b3=output storage image, b4=GI instance SSBO
    m_textureSetLayout = core::DescriptorSetLayout::createShared(
        device,
        std::vector<VkDescriptorSetLayoutBinding>{
            makeBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, rgenStages),
            makeBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, rgenStages),
            makeBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, rgenStages),
            makeBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          rgenStages),
            makeBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         chitStages)});

    VkPushConstantRange pcRange = PushConstant<RTIndirectDiffusePC>::getRange(
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
            *EngineShaderFamilies::cameraDescriptorSetLayout,
            *m_textureSetLayout},
        std::vector<VkPushConstantRange>{pcRange});

    m_sampler     = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_depthSampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    // Pre-create minimum-size GI instance buffers so compile() can bind slot 4 immediately.
    const uint32_t giBufferCount = std::max(imageCount, kMinGISceneBufferCount);
    m_giSceneBuffers.resize(giBufferCount);
    m_giSceneBufferSizes.assign(giBufferCount, 0u);
    for (uint32_t j = 0; j < giBufferCount; ++j)
    {
        m_giSceneBuffers[j] = core::Buffer::createShared(
            kMinGISceneBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU);
        m_giSceneBufferSizes[j] = kMinGISceneBufferSize;
    }

    if (core::VulkanContext::getContext()->hasRayTracingPipelineSupport())
        createRayTracingPipeline();
}

void RTIndirectDiffuseRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputRenderTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    const auto *depthTarget = storage.getTexture(m_depthHandler);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        const auto *normalTarget = storage.getTexture(m_normalHandlers[i]);
        const auto *albedoTarget = storage.getTexture(m_albedoHandlers[i]);
        const auto *outputTarget = storage.getTexture(m_outputHandlers[i]);
        m_outputRenderTargets[i] = outputTarget;

        const uint32_t bufIdx = i % static_cast<uint32_t>(m_giSceneBuffers.size());

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(normalTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .addImage(albedoTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                                      .addImage(depthTarget->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 2)
                                      .addStorageImage(outputTarget->vkImageView(), VK_IMAGE_LAYOUT_GENERAL, 3)
                                      .addBuffer(m_giSceneBuffers[bufIdx], m_giSceneBufferSizes[bufIdx], 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                      .build(core::VulkanContext::getContext()->getDevice(),
                                             core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                             m_textureSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(normalTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(albedoTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .addImage(depthTarget->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 2)
                .addStorageImage(outputTarget->vkImageView(), VK_IMAGE_LAYOUT_GENERAL, 3)
                .addBuffer(m_giSceneBuffers[bufIdx], m_giSceneBufferSizes[bufIdx], 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void RTIndirectDiffuseRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                              const RenderGraphPassPerFrameData  &data,
                                              const RenderGraphPassContext       &renderContext)
{
    if (!canUsePipelinePath())
        return;

    const auto *outputTarget = m_outputRenderTargets[renderContext.currentImageIndex];
    if (!outputTarget)
        return;

    // Skip if no geometry in scene (empty TLAS)
    if (data.drawBatches.empty())
    {
        VkClearColorValue clear{};
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(commandBuffer->vk(), outputTarget->getImage()->vk(),
                             VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
        return;
    }

    const glm::vec3 towardSun = data.hasDirectionalLight
                                    ? -glm::normalize(data.directionalLightDirection)
                                    : glm::vec3(0.0f, 1.0f, 0.0f);

    RTIndirectDiffusePC pc{};
    pc.giSamples   = std::clamp(RenderQualitySettings::getInstance().giSamples, 1, 16);
    pc.sunHeight   = glm::clamp(glm::dot(towardSun, glm::vec3(0.0f, 1.0f, 0.0f)), -1.0f, 1.0f);
    pc.frameOffset = glm::fract(static_cast<float>(m_frameCounter) * 0.00137438953f);
    pc.pad         = 0.0f;
    ++m_frameCounter;

    // Populate instance SSBO for the rchit shader and update descriptor binding 4.
    updateGISceneBuffer(data, renderContext.currentFrame);
    if (renderContext.currentFrame < m_giSceneBuffers.size() && m_giSceneBuffers[renderContext.currentFrame])
    {
        DescriptorSetBuilder::begin()
            .addBuffer(m_giSceneBuffers[renderContext.currentFrame],
                       m_giSceneBufferSizes[renderContext.currentFrame],
                       4,
                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update(core::VulkanContext::getContext()->getDevice(),
                    m_descriptorSets[renderContext.currentImageIndex]);
    }

    const VkPipelineLayout rtLayout =
        (m_rtPipelineLayout != VK_NULL_HANDLE) ? m_rtPipelineLayout : m_pipelineLayout->vk();

    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rayTracingPipeline->vk());

    if (m_rtPipelineLayout != VK_NULL_HANDLE && data.bindlessDescriptorSet != VK_NULL_HANDLE)
    {
        VkDescriptorSet rtSets[3] = {
            data.cameraDescriptorSet,
            m_descriptorSets[renderContext.currentImageIndex],
            data.bindlessDescriptorSet};
        vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                rtLayout, 0, 3, rtSets, 0, nullptr);
    }
    else
    {
        VkDescriptorSet sets[2] = {data.cameraDescriptorSet, m_descriptorSets[renderContext.currentImageIndex]};
        vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                rtLayout, 0, 2, sets, 0, nullptr);
    }

    vkCmdPushConstants(commandBuffer->vk(), rtLayout,
                       VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
                       0, sizeof(pc), &pc);
    vkCmdTraceRaysKHR(commandBuffer->vk(),
                      &m_shaderBindingTable->raygenRegion(),
                      &m_shaderBindingTable->missRegion(),
                      &m_shaderBindingTable->hitRegion(),
                      &m_shaderBindingTable->callableRegion(),
                      m_extent.width, m_extent.height, 1);
}

std::vector<IRenderGraphPass::RenderPassExecution>
RTIndirectDiffuseRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.mode             = IRenderGraphPass::ExecutionMode::Direct;
    exec.useDepth         = false;
    exec.depthFormat      = VK_FORMAT_UNDEFINED;
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;
    exec.targets[m_outputHandlers[renderContext.currentImageIndex]] = m_outputRenderTargets[renderContext.currentImageIndex];
    return {exec};
}

void RTIndirectDiffuseRenderGraphPass::setExtent(VkExtent2D extent)
{
    if (m_extent.width == extent.width && m_extent.height == extent.height)
        return;
    m_extent = extent;
    requestRecompilation();
}

void RTIndirectDiffuseRenderGraphPass::cleanup()
{
    destroyRayTracingPipeline();
    m_giSceneBuffers.clear();
    m_giSceneBufferSizes.clear();
}

void RTIndirectDiffuseRenderGraphPass::freeResources()
{
    m_outputRenderTargets.clear();
    for (auto &s : m_descriptorSets) s = VK_NULL_HANDLE;
    m_descriptorSetsInitialized = false;
    outputs.color.set(MultiHandle{});
}

void RTIndirectDiffuseRenderGraphPass::updateGISceneBuffer(const RenderGraphPassPerFrameData &data, uint32_t frameIndex)
{
    if (frameIndex >= m_giSceneBuffers.size())
        return;

    const VkDeviceSize dataSize = static_cast<VkDeviceSize>(
        data.rtReflectionShadingInstances.size() * sizeof(RTReflectionShadingInstanceData));
    const VkDeviceSize requiredSize = std::max(dataSize, kMinGISceneBufferSize);

    if (!m_giSceneBuffers[frameIndex] || m_giSceneBufferSizes[frameIndex] < requiredSize)
    {
        m_giSceneBuffers[frameIndex] = core::Buffer::createShared(
            requiredSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU);
        m_giSceneBufferSizes[frameIndex] = requiredSize;
    }

    if (dataSize > 0u)
        m_giSceneBuffers[frameIndex]->upload(data.rtReflectionShadingInstances.data(), dataSize);
    else
    {
        std::array<uint32_t, 4> zeros{0u, 0u, 0u, 0u};
        m_giSceneBuffers[frameIndex]->upload(zeros.data(), sizeof(zeros));
    }
}

void RTIndirectDiffuseRenderGraphPass::createRayTracingPipeline()
{
    destroyRayTracingPipeline();

    auto context = core::VulkanContext::getContext();
    if (!context || !context->hasRayTracingPipelineSupport())
        return;

    // Build 3-set layout for the rchit path: set 0=camera, set 1=GI textures, set 2=bindless.
    if (m_textureSetLayout && EngineShaderFamilies::bindlessMaterialSetLayout != VK_NULL_HANDLE)
    {
        const VkDescriptorSetLayout setLayouts[3] = {
            EngineShaderFamilies::cameraDescriptorSetLayout->vk(),
            m_textureSetLayout->vk(),
            EngineShaderFamilies::bindlessMaterialSetLayout};

        const VkPushConstantRange pcRange =
            PushConstant<RTIndirectDiffusePC>::getRange(
                VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount         = 3;
        layoutInfo.pSetLayouts            = setLayouts;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges    = &pcRange;

        vkCreatePipelineLayout(context->getDevice(), &layoutInfo, nullptr, &m_rtPipelineLayout);
    }

    m_raygenShader.loadFromFile("./resources/shaders/rt_indirect_diffuse.rgen.spv", core::ShaderStage::RAYGEN);
    m_missShader.loadFromFile("./resources/shaders/rt_indirect_diffuse.rmiss.spv", core::ShaderStage::MISS);
    m_closestHitShader.loadFromFile("./resources/shaders/rt_indirect_diffuse.rchit.spv", core::ShaderStage::CLOSEST_HIT);

    const std::array<VkPipelineShaderStageCreateInfo, 3> stages = {
        m_raygenShader.getInfo(),
        m_missShader.getInfo(),
        m_closestHitShader.getInfo()};

    std::array<VkRayTracingShaderGroupCreateInfoKHR, 3> groups{};

    groups[0].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[0].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[0].generalShader      = 0;
    groups[0].closestHitShader   = VK_SHADER_UNUSED_KHR;
    groups[0].anyHitShader       = VK_SHADER_UNUSED_KHR;
    groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

    groups[1].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[1].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[1].generalShader      = 1;
    groups[1].closestHitShader   = VK_SHADER_UNUSED_KHR;
    groups[1].anyHitShader       = VK_SHADER_UNUSED_KHR;
    groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

    groups[2].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[2].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    groups[2].generalShader      = VK_SHADER_UNUSED_KHR;
    groups[2].closestHitShader   = 2;
    groups[2].anyHitShader       = VK_SHADER_UNUSED_KHR;
    groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

    VkRayTracingPipelineCreateInfoKHR pipelineCI{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    pipelineCI.stageCount                   = static_cast<uint32_t>(stages.size());
    pipelineCI.pStages                      = stages.data();
    pipelineCI.groupCount                   = static_cast<uint32_t>(groups.size());
    pipelineCI.pGroups                      = groups.data();
    pipelineCI.maxPipelineRayRecursionDepth = 1;
    pipelineCI.layout = (m_rtPipelineLayout != VK_NULL_HANDLE) ? m_rtPipelineLayout : m_pipelineLayout->vk();

    m_rayTracingPipeline = core::rtx::RayTracingPipeline::create(
        std::vector<VkPipelineShaderStageCreateInfo>{stages.begin(), stages.end()},
        std::vector<VkRayTracingShaderGroupCreateInfoKHR>{groups.begin(), groups.end()},
        pipelineCI.layout,
        1u);

    if (!m_rayTracingPipeline || !m_rayTracingPipeline->isValid())
        throw std::runtime_error("Failed to create RT indirect diffuse pipeline");

    createShaderBindingTable();
}

void RTIndirectDiffuseRenderGraphPass::createShaderBindingTable()
{
    m_shaderBindingTable = core::rtx::ShaderBindingTable::create(*m_rayTracingPipeline, {0u}, {1u}, {2u});
    if (!m_shaderBindingTable || !m_shaderBindingTable->isValid())
        throw std::runtime_error("Failed to create RT GI shader binding table");
}

void RTIndirectDiffuseRenderGraphPass::destroyRayTracingPipeline()
{
    auto context = core::VulkanContext::getContext();
    m_rayTracingPipeline.reset();
    if (context && m_rtPipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(context->getDevice(), m_rtPipelineLayout, nullptr);
        m_rtPipelineLayout = VK_NULL_HANDLE;
    }

    m_shaderBindingTable.reset();

    m_closestHitShader.destroyVk();
    m_missShader.destroyVk();
    m_raygenShader.destroyVk();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
