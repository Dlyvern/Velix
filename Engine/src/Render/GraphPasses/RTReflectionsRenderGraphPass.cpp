#include "Engine/Render/GraphPasses/RTReflectionsRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include <algorithm>
#include <filesystem>
#include <glm/glm.hpp>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
    constexpr uint32_t kMinReflectionSceneBufferCount = 2u;
    constexpr VkDeviceSize kMinReflectionSceneBufferSize = 16u;
    constexpr VkShaderStageFlags kRasterPushConstantStages =
        VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    constexpr VkShaderStageFlags kRtPushConstantStages =
        VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR |
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;

    static_assert(offsetof(RTReflectionShadingInstanceData, material) == 32u);
    static_assert(sizeof(Material::GPUParams) == 96u);
}

struct RTReflectionsPC
{
    float enableRTReflections;
    float rtReflectionSamples;
    float rtRoughnessThreshold;
    float rtReflectionStrength;
    glm::vec3 sunDirection;    // world-space direction TO sun (for sky fallback)
    float sunHeight;           // dot(sunDir, up) clamped [-1,1]
    glm::vec4 environmentInfo; // x = hasEnvironmentMap
};

bool RTReflectionsRenderGraphPass::isEnabled() const
{
    const auto &s = RenderQualitySettings::getInstance();
    auto context = core::VulkanContext::getContext();

    if (!context || !s.enableRayTracing || !s.enableRTReflections)
        return false;

    if (s.rayTracingMode == RenderQualitySettings::RayTracingMode::Pipeline)
        return canUsePipelinePath() || context->hasRayQuerySupport();

    return s.rayTracingMode == RenderQualitySettings::RayTracingMode::RayQuery &&
           context->hasRayQuerySupport();
}

uint64_t RTReflectionsRenderGraphPass::getExecutionCacheKey(const RenderGraphPassContext &) const
{
    return shouldUsePipelinePath() ? 1u : 0u;
}

RTReflectionsRenderGraphPass::RTReflectionsRenderGraphPass(
    std::vector<RGPResourceHandler> &lightingHandlers,
    std::vector<RGPResourceHandler> &normalHandlers,
    std::vector<RGPResourceHandler> &albedoHandlers,
    std::vector<RGPResourceHandler> &materialHandlers,
    RGPResourceHandler &depthHandler)
    : m_lightingHandlers(lightingHandlers),
      m_normalHandlers(normalHandlers),
      m_albedoHandlers(albedoHandlers),
      m_materialHandlers(materialHandlers),
      m_depthHandler(depthHandler)
{
    setDebugName("RT Reflections render graph pass");
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    outputs.color.setOwner(this);
}

void RTReflectionsRenderGraphPass::prepareRecord(const RenderGraphPassPerFrameData &data,
                                                 const RenderGraphPassContext &)
{
    if (m_requestedSkyboxHDRPath != data.skyboxHDRPath)
    {
        m_requestedSkyboxHDRPath = data.skyboxHDRPath;
        m_pendingSkyboxUpdate = true;
        requestRecompilation();
    }
}

bool RTReflectionsRenderGraphPass::canUsePipelinePath() const
{
    auto context = core::VulkanContext::getContext();
    return context &&
           context->hasRayTracingPipelineSupport() &&
           m_rayTracingPipeline &&
           m_rayTracingPipeline->isValid() &&
           m_shaderBindingTable &&
           m_shaderBindingTable->isValid();
}

bool RTReflectionsRenderGraphPass::shouldUsePipelinePath() const
{
    const auto &s = RenderQualitySettings::getInstance();
    return s.enableRayTracing &&
           s.enableRTReflections &&
           s.rayTracingMode == RenderQualitySettings::RayTracingMode::Pipeline &&
           canUsePipelinePath();
}

void RTReflectionsRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    const uint32_t reflectionSceneBufferCount = std::max(imageCount, kMinReflectionSceneBufferCount);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        builder.read(m_lightingHandlers[i], RGPTextureUsage::SAMPLED);
        builder.read(m_normalHandlers[i], RGPTextureUsage::SAMPLED);
        builder.read(m_albedoHandlers[i], RGPTextureUsage::SAMPLED);
        builder.read(m_materialHandlers[i], RGPTextureUsage::SAMPLED);
    }
    builder.read(m_depthHandler, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_colorFormat, RGPTextureUsage::COLOR_ATTACHMENT_STORAGE};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_GENERAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this]
                                    { return m_extent; });

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_RT_REFLECTIONS_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT_STORAGE);
    }
    outputs.color.set(m_outputHandlers);

    auto device = core::VulkanContext::getContext()->getDevice();

    auto makeBinding = [](uint32_t binding, VkDescriptorType type, VkShaderStageFlags stages) -> VkDescriptorSetLayoutBinding
    {
        VkDescriptorSetLayoutBinding descriptorSetLayoutBinding{};
        descriptorSetLayoutBinding.binding = binding;
        descriptorSetLayoutBinding.descriptorType = type;
        descriptorSetLayoutBinding.descriptorCount = 1;
        descriptorSetLayoutBinding.stageFlags = stages;
        return descriptorSetLayoutBinding;
    };

    const VkShaderStageFlags sampledStages = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    const VkShaderStageFlags environmentStages =
        VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    const VkShaderStageFlags reflectionInstanceStages =
        VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        makeBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampledStages),         // normal
        makeBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampledStages),         // albedo
        makeBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampledStages),         // material
        makeBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampledStages),         // depth
        makeBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampledStages),         // lighting
        makeBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR), // output
        makeBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, reflectionInstanceStages),      // RT shading instances
        makeBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, environmentStages),     // environment cubemap
    };

    m_textureSetLayout = core::DescriptorSetLayout::createShared(device, bindings);

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
            *EngineShaderFamilies::cameraDescriptorSetLayout,
            *m_textureSetLayout},
        std::vector<VkPushConstantRange>{
            PushConstant<RTReflectionsPC>::getRange(kRasterPushConstantStages)});

    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_depthSampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_reflectionSceneBuffers.resize(reflectionSceneBufferCount);
    m_reflectionSceneBufferSizes.assign(reflectionSceneBufferCount, 0u);

    // Pre-create minimum-size buffers so compile() can bind slot 6 immediately.
    for (uint32_t j = 0; j < reflectionSceneBufferCount; ++j)
    {
        m_reflectionSceneBuffers[j] = core::Buffer::createShared(
            kMinReflectionSceneBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU);
        m_reflectionSceneBufferSizes[j] = kMinReflectionSceneBufferSize;
    }

    if (core::VulkanContext::getContext()->hasRayTracingPipelineSupport())
        createRayTracingPipeline();
}

void RTReflectionsRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    ensureFallbackEnvironmentTexture();
    updateEnvironmentSkybox();

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputRenderTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    auto depthTarget = storage.getTexture(m_depthHandler);
    const VkImageView environmentImageView =
        (m_environmentSkybox && m_environmentSkybox->hasTexture())
            ? m_environmentSkybox->getEnvImageView()
            : (m_fallbackEnvironmentTexture ? m_fallbackEnvironmentTexture->vkImageView() : VK_NULL_HANDLE);
    const VkSampler environmentSampler =
        (m_environmentSkybox && m_environmentSkybox->hasTexture())
            ? m_environmentSkybox->getEnvSampler()
            : (m_fallbackEnvironmentTexture ? m_fallbackEnvironmentTexture->vkSampler() : VK_NULL_HANDLE);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputRenderTargets[i] = storage.getTexture(m_outputHandlers[i]);

        auto normalTarget = storage.getTexture(m_normalHandlers[i]);
        auto albedoTarget = storage.getTexture(m_albedoHandlers[i]);
        auto materialTarget = storage.getTexture(m_materialHandlers[i]);
        auto lightingTarget = storage.getTexture(m_lightingHandlers[i]);
        auto outputTarget = m_outputRenderTargets[i];

        const uint32_t bufIdx = static_cast<uint32_t>(i) % static_cast<uint32_t>(m_reflectionSceneBuffers.size());

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(normalTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .addImage(albedoTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                                      .addImage(materialTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                                      .addImage(depthTarget->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 3)
                                      .addImage(lightingTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 4)
                                      .addStorageImage(outputTarget->vkImageView(), VK_IMAGE_LAYOUT_GENERAL, 5)
                                      .addBuffer(m_reflectionSceneBuffers[bufIdx], m_reflectionSceneBufferSizes[bufIdx], 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                      .addImage(environmentImageView, environmentSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 7)
                                      .build(core::VulkanContext::getContext()->getDevice(),
                                             core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                             m_textureSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(normalTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(albedoTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .addImage(materialTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                .addImage(depthTarget->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 3)
                .addImage(lightingTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 4)
                .addStorageImage(outputTarget->vkImageView(), VK_IMAGE_LAYOUT_GENERAL, 5)
                .addBuffer(m_reflectionSceneBuffers[bufIdx], m_reflectionSceneBufferSizes[bufIdx], 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .addImage(environmentImageView, environmentSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 7)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void RTReflectionsRenderGraphPass::updateReflectionSceneBuffer(const RenderGraphPassPerFrameData &data, uint32_t frameIndex)
{
    if (frameIndex >= m_reflectionSceneBuffers.size())
        return;

    const VkDeviceSize dataSize =
        static_cast<VkDeviceSize>(data.rtReflectionShadingInstances.size() * sizeof(RTReflectionShadingInstanceData));
    const VkDeviceSize requiredSize = std::max(dataSize, kMinReflectionSceneBufferSize);

    if (!m_reflectionSceneBuffers[frameIndex] || m_reflectionSceneBufferSizes[frameIndex] < requiredSize)
    {
        m_reflectionSceneBuffers[frameIndex] = core::Buffer::createShared(
            requiredSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU);
        m_reflectionSceneBufferSizes[frameIndex] = requiredSize;
    }

    if (dataSize > 0u)
    {
        m_reflectionSceneBuffers[frameIndex]->upload(data.rtReflectionShadingInstances.data(), dataSize);
    }
    else
    {
        std::array<uint32_t, 4> zeros{0u, 0u, 0u, 0u};
        m_reflectionSceneBuffers[frameIndex]->upload(zeros.data(), sizeof(zeros));
    }
}

void RTReflectionsRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                          const RenderGraphPassPerFrameData &data,
                                          const RenderGraphPassContext &renderContext)
{
    // TLAS is only valid when there are RT instances. Never enable ray tracing without it —
    // accessing an unbound acceleration structure descriptor causes GPU errors.
    const bool hasTLAS = !data.rtReflectionShadingInstances.empty();

    if (data.drawBatches.empty())
    {
        // Pipeline path: must clear the storage image manually (no renderpass).
        // Ray-query path: output is a color attachment cleared by vkCmdBeginRendering.
        // In both cases, output must be deterministic (not undefined) so the denoise pass
        // doesn't read garbage on the first frame while the scene is still loading.
        const auto *outputTarget = m_outputRenderTargets[renderContext.currentImageIndex];
        if (outputTarget)
        {
            VkClearColorValue clearColor{};
            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;
            vkCmdClearColorImage(commandBuffer->vk(), outputTarget->getImage()->vk(),
                                 VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);
        }
        return;
    }

    const auto &s = RenderQualitySettings::getInstance();
    const bool rayQueryActive =
        hasTLAS &&
        s.enableRayTracing &&
        s.enableRTReflections &&
        (s.rayTracingMode == RenderQualitySettings::RayTracingMode::RayQuery ||
         (s.rayTracingMode == RenderQualitySettings::RayTracingMode::Pipeline && !shouldUsePipelinePath())) &&
        core::VulkanContext::getContext()->hasRayQuerySupport();

    // directionalLightDirection is the direction the light POINTS (toward scene).
    // For sky shading we need the direction TOWARD the sun (negated).
    const glm::vec3 towardSun = data.hasDirectionalLight
                                    ? -glm::normalize(data.directionalLightDirection)
                                    : glm::vec3(0.0f, 1.0f, 0.0f);

    RTReflectionsPC pc{};
    pc.enableRTReflections = (rayQueryActive || (hasTLAS && shouldUsePipelinePath())) ? 1.0f : 0.0f;
    pc.rtReflectionSamples = pc.enableRTReflections > 0.5f ? static_cast<float>(std::clamp(s.rtReflectionSamples, 1, 8)) : 1.0f;
    pc.rtRoughnessThreshold = pc.enableRTReflections > 0.5f ? std::clamp(s.rtRoughnessThreshold, 0.0f, 1.0f) : 0.0f;
    pc.rtReflectionStrength = pc.enableRTReflections > 0.5f ? std::clamp(s.rtReflectionStrength, 0.0f, 2.0f) : 0.0f;
    pc.sunDirection = towardSun;
    pc.sunHeight = glm::clamp(glm::dot(towardSun, glm::vec3(0.0f, 1.0f, 0.0f)), -1.0f, 1.0f);
    pc.environmentInfo = glm::vec4(
        (m_environmentSkybox && m_environmentSkybox->hasTexture()) ? 1.0f : 0.0f,
        0.0f, 0.0f, 0.0f);

    VkDescriptorSet sets[2] = {
        data.cameraDescriptorSet,
        m_descriptorSets[renderContext.currentImageIndex]};

    // Always update the instance SSBO (slot 6) — needed by BOTH pipeline and ray-query paths.
    updateReflectionSceneBuffer(data, renderContext.currentFrame);
    if (renderContext.currentFrame < m_reflectionSceneBuffers.size() &&
        m_reflectionSceneBuffers[renderContext.currentFrame])
    {
        DescriptorSetBuilder::begin()
            .addBuffer(m_reflectionSceneBuffers[renderContext.currentFrame],
                       m_reflectionSceneBufferSizes[renderContext.currentFrame],
                       6,
                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[renderContext.currentImageIndex]);
    }

    if (shouldUsePipelinePath())
    {
        const VkPipelineLayout rtLayout =
            (m_rtPipelineLayout != VK_NULL_HANDLE) ? m_rtPipelineLayout : m_pipelineLayout->vk();
        const VkShaderStageFlags pushStages =
            (m_rtPipelineLayout != VK_NULL_HANDLE) ? kRtPushConstantStages : kRasterPushConstantStages;

        vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rayTracingPipeline->vk());

        if (m_rtPipelineLayout != VK_NULL_HANDLE && data.bindlessDescriptorSet != VK_NULL_HANDLE)
        {
            VkDescriptorSet rtSets[3] = {
                data.cameraDescriptorSet,
                m_descriptorSets[renderContext.currentImageIndex],
                data.bindlessDescriptorSet};
            vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtLayout,
                                    0, 3, rtSets, 0, nullptr);
        }
        else
        {
            vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtLayout,
                                    0, 2, sets, 0, nullptr);
        }

        vkCmdPushConstants(commandBuffer->vk(), rtLayout, pushStages, 0, sizeof(pc), &pc);
        vkCmdTraceRaysKHR(commandBuffer->vk(),
                          &m_shaderBindingTable->raygenRegion(),
                          &m_shaderBindingTable->missRegion(),
                          &m_shaderBindingTable->hitRegion(),
                          &m_shaderBindingTable->callableRegion(),
                          m_extent.width,
                          m_extent.height,
                          1);
        return;
    }

    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    GraphicsPipelineKey key{};
    key.shader = ShaderId::RTReflections;
    key.cull = CullMode::None;
    key.depthTest = false;
    key.depthWrite = false;
    key.depthCompare = VK_COMPARE_OP_LESS;
    key.polygonMode = VK_POLYGON_MODE_FILL;
    key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats = {m_colorFormat};
    key.pipelineLayout = m_pipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                            0, 2, sets, 0, nullptr);
    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout, kRasterPushConstantStages, 0, sizeof(pc), &pc);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<IRenderGraphPass::RenderPassExecution> RTReflectionsRenderGraphPass::getRenderPassExecutions(
    const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;
    exec.targets[m_outputHandlers[renderContext.currentImageIndex]] =
        m_outputRenderTargets[renderContext.currentImageIndex];

    if (shouldUsePipelinePath())
    {
        exec.mode = IRenderGraphPass::ExecutionMode::Direct;
        exec.useDepth = false;
        exec.depthFormat = VK_FORMAT_UNDEFINED;
        return {exec};
    }

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_outputRenderTargets[renderContext.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = m_clearValues[0];

    exec.colorsRenderingItems = {color};
    exec.useDepth = false;
    exec.colorFormats = {m_colorFormat};
    exec.depthFormat = VK_FORMAT_UNDEFINED;

    return {exec};
}

void RTReflectionsRenderGraphPass::setExtent(VkExtent2D extent)
{
    if (m_extent.width == extent.width && m_extent.height == extent.height)
        return;

    m_extent = extent;
    m_viewport = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    m_scissor = {{0, 0}, extent};
    requestRecompilation();
}

void RTReflectionsRenderGraphPass::cleanup()
{
    destroyRayTracingPipeline();
    m_reflectionSceneBuffers.clear();
    m_reflectionSceneBufferSizes.clear();
    m_environmentSkybox.reset();
    m_fallbackEnvironmentTexture.reset();
    m_requestedSkyboxHDRPath.clear();
    m_loadedSkyboxHDRPath.clear();
    m_pendingSkyboxUpdate = true;
}

void RTReflectionsRenderGraphPass::ensureFallbackEnvironmentTexture()
{
    if (m_fallbackEnvironmentTexture)
        return;

    m_fallbackEnvironmentTexture = std::make_shared<Texture>();

    constexpr int width = 4;
    constexpr int height = 2;
    const float equirectangularData[width * height * 3] = {
        0.78f,
        0.80f,
        0.84f,
        0.78f,
        0.80f,
        0.84f,
        0.78f,
        0.80f,
        0.84f,
        0.78f,
        0.80f,
        0.84f,
        0.42f,
        0.44f,
        0.48f,
        0.42f,
        0.44f,
        0.48f,
        0.42f,
        0.44f,
        0.48f,
        0.42f,
        0.44f,
        0.48f,
    };

    if (!m_fallbackEnvironmentTexture->createCubemapFromEquirectangular(equirectangularData, width, height, 16u))
        throw std::runtime_error("Failed to create fallback RT reflection environment texture");
}

void RTReflectionsRenderGraphPass::updateEnvironmentSkybox()
{
    if (!m_pendingSkyboxUpdate)
        return;

    m_pendingSkyboxUpdate = false;

    if (m_requestedSkyboxHDRPath == m_loadedSkyboxHDRPath)
        return;

    if (m_requestedSkyboxHDRPath.empty())
    {
        m_environmentSkybox.reset();
        m_loadedSkyboxHDRPath.clear();
        return;
    }

    if (!std::filesystem::exists(m_requestedSkyboxHDRPath))
    {
        VX_ENGINE_WARNING_STREAM("RT reflections skybox HDR file was not found: " << m_requestedSkyboxHDRPath << '\n');
        m_environmentSkybox.reset();
        m_loadedSkyboxHDRPath.clear();
        return;
    }

    auto candidate = std::make_unique<Skybox>(
        m_requestedSkyboxHDRPath,
        core::VulkanContext::getContext()->getPersistentDescriptorPool()->vk());

    if (!candidate->hasTexture())
    {
        VX_ENGINE_WARNING_STREAM("RT reflections failed to load environment map: " << m_requestedSkyboxHDRPath
                                                                                   << ". Falling back to procedural sky.\n");
        m_environmentSkybox.reset();
        m_loadedSkyboxHDRPath.clear();
        return;
    }

    m_environmentSkybox = std::move(candidate);
    m_loadedSkyboxHDRPath = m_requestedSkyboxHDRPath;
}

void RTReflectionsRenderGraphPass::createRayTracingPipeline()
{
    destroyRayTracingPipeline(); // also clears m_rtPipelineLayout

    auto context = core::VulkanContext::getContext();
    if (!context || !context->hasRayTracingPipelineSupport())
        return;

    // Build 3-set layout: set 0 = camera, set 1 = RT textures, set 2 = bindless.
    // Must be created AFTER destroyRayTracingPipeline() and BEFORE vkCreateRayTracingPipelinesKHR.
    if (m_textureSetLayout && EngineShaderFamilies::bindlessMaterialSetLayout != VK_NULL_HANDLE)
    {
        const VkDescriptorSetLayout setLayouts[3] = {
            EngineShaderFamilies::cameraDescriptorSetLayout->vk(),
            m_textureSetLayout->vk(),
            EngineShaderFamilies::bindlessMaterialSetLayout};

        const VkPushConstantRange pcRange =
            PushConstant<RTReflectionsPC>::getRange(kRtPushConstantStages);

        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 3;
        layoutInfo.pSetLayouts = setLayouts;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pcRange;

        vkCreatePipelineLayout(context->getDevice(), &layoutInfo, nullptr, &m_rtPipelineLayout);
    }

    m_raygenShader.loadFromFile("./resources/shaders/rt_reflections.rgen.spv", core::ShaderStage::RAYGEN);
    m_missShader.loadFromFile("./resources/shaders/rt_reflections.rmiss.spv", core::ShaderStage::MISS);
    m_closestHitShader.loadFromFile("./resources/shaders/rt_reflections.rchit.spv", core::ShaderStage::CLOSEST_HIT);
    m_anyHitShader.loadFromFile("./resources/shaders/rt_reflections.rahit.spv", core::ShaderStage::ANY_HIT);

    // Stage indices: 0=rgen, 1=rmiss, 2=rchit, 3=rahit
    const std::array<VkPipelineShaderStageCreateInfo, 4> shaderStages = {
        m_raygenShader.getInfo(),
        m_missShader.getInfo(),
        m_closestHitShader.getInfo(),
        m_anyHitShader.getInfo()};

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

    // Hit group: rchit (stage 2) + rahit (stage 3).
    // For FORCE_OPAQUE instances (opaque geometry) the any-hit is skipped automatically.
    // For non-opaque alpha-masked instances, the any-hit performs the alpha cutoff test.
    shaderGroups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    shaderGroups[2].generalShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[2].closestHitShader = 2;
    shaderGroups[2].anyHitShader = 3;
    shaderGroups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

    VkRayTracingPipelineCreateInfoKHR pipelineCreateInfo{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCreateInfo.pStages = shaderStages.data();
    pipelineCreateInfo.groupCount = static_cast<uint32_t>(shaderGroups.size());
    pipelineCreateInfo.pGroups = shaderGroups.data();
    pipelineCreateInfo.maxPipelineRayRecursionDepth = 1;
    pipelineCreateInfo.layout = (m_rtPipelineLayout != VK_NULL_HANDLE) ? m_rtPipelineLayout : m_pipelineLayout->vk();

    m_rayTracingPipeline = core::rtx::RayTracingPipeline::create(
        std::vector<VkPipelineShaderStageCreateInfo>{shaderStages.begin(), shaderStages.end()},
        std::vector<VkRayTracingShaderGroupCreateInfoKHR>{shaderGroups.begin(), shaderGroups.end()},
        pipelineCreateInfo.layout,
        1u);

    if (!m_rayTracingPipeline || !m_rayTracingPipeline->isValid())
        throw std::runtime_error("Failed to create RT reflections pipeline");

    createShaderBindingTable();
}

void RTReflectionsRenderGraphPass::createShaderBindingTable()
{
    m_shaderBindingTable = core::rtx::ShaderBindingTable::create(*m_rayTracingPipeline, {0u}, {1u}, {2u});
    if (!m_shaderBindingTable || !m_shaderBindingTable->isValid())
        throw std::runtime_error("Failed to create RT reflections shader binding table");
}

void RTReflectionsRenderGraphPass::destroyRayTracingPipeline()
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
    m_anyHitShader.destroyVk();
}

void RTReflectionsRenderGraphPass::freeResources()
{
    m_outputRenderTargets.clear();
    for (auto &s : m_descriptorSets)
        s = VK_NULL_HANDLE;
    m_descriptorSetsInitialized = false;
    outputs.color.set(MultiHandle{});
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
