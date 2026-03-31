#include "Engine/Render/GraphPasses/VolumetricFogLightingRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

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

    struct FogLightingPC
    {
        glm::vec4 fogColorDensity;
        glm::vec4 fogParams0;
        glm::vec4 fogParams1;
        glm::vec4 fogParams2;
        glm::vec4 extentInfo;
    };
} // namespace

VolumetricFogLightingRenderGraphPass::VolumetricFogLightingRenderGraphPass(
    RGPResourceHandler &depthTextureHandler,
    RGPResourceHandler &directionalShadowHandler,
    RGPResourceHandler &cubeShadowHandler,
    RGPResourceHandler &spotShadowHandler)
    : m_depthTextureHandler(depthTextureHandler),
      m_directionalShadowHandler(directionalShadowHandler),
      m_cubeShadowHandler(cubeShadowHandler),
      m_spotShadowHandler(spotShadowHandler)
{
    setDebugName("Volumetric fog lighting render graph pass");
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 0.0f};
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    outputs.color.setOwner(this);
}

void VolumetricFogLightingRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_outputHandlers.clear();

    builder.read(m_depthTextureHandler, RGPTextureUsage::SAMPLED);
    builder.read(m_directionalShadowHandler, RGPTextureUsage::SAMPLED);
    builder.read(m_spotShadowHandler, RGPTextureUsage::SAMPLED);
    builder.read(m_cubeShadowHandler, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this] { return m_extent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_VOLUMETRIC_FOG_LIGHTING_" + std::to_string(i) + "__");
        auto handler = builder.createTexture(outDesc);
        m_outputHandlers.push_back(handler);
        builder.write(handler, RGPTextureUsage::COLOR_ATTACHMENT);
    }
    outputs.color.set(m_outputHandlers);

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding depthBinding{};
    depthBinding.binding = 0;
    depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthBinding.descriptorCount = 1;
    depthBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding directionalBinding{};
    directionalBinding.binding = 1;
    directionalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    directionalBinding.descriptorCount = 1;
    directionalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding spotBinding{};
    spotBinding.binding = 2;
    spotBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    spotBinding.descriptorCount = 1;
    spotBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding pointBinding{};
    pointBinding.binding = 3;
    pointBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pointBinding.descriptorCount = 1;
    pointBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(
        device,
        std::vector<VkDescriptorSetLayoutBinding>{depthBinding, directionalBinding, spotBinding, pointBinding});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
            *EngineShaderFamilies::cameraDescriptorSetLayout,
            *m_descriptorSetLayout},
        std::vector<VkPushConstantRange>{
            PushConstant<FogLightingPC>::getRange(VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_depthSampler = core::Sampler::createShared(
        VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_shadowSampler = core::Sampler::createShared(
        VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE, VK_COMPARE_OP_ALWAYS, VK_SAMPLER_MIPMAP_MODE_LINEAR);
}

void VolumetricFogLightingRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    const auto *depthTexture = storage.getTexture(m_depthTextureHandler);
    const auto *directionalShadowTexture = storage.getTexture(m_directionalShadowHandler);
    const auto *spotShadowTexture = storage.getTexture(m_spotShadowHandler);
    const auto *pointShadowTexture = storage.getTexture(m_cubeShadowHandler);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputTargets[i] = storage.getTexture(m_outputHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(depthTexture->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 0)
                                      .addImage(directionalShadowTexture->vkImageView(), m_shadowSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                                      .addImage(spotShadowTexture->vkImageView(), m_shadowSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 2)
                                      .addImage(pointShadowTexture->vkImageView(), m_shadowSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 3)
                                      .build(core::VulkanContext::getContext()->getDevice(),
                                             core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                             m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(depthTexture->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 0)
                .addImage(directionalShadowTexture->vkImageView(), m_shadowSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                .addImage(spotShadowTexture->vkImageView(), m_shadowSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 2)
                .addImage(pointShadowTexture->vkImageView(), m_shadowSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 3)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void VolumetricFogLightingRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                                  const RenderGraphPassPerFrameData &data,
                                                  const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    GraphicsPipelineKey key{};
    key.shader = ShaderId::VolumetricFogLighting;
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
        m_descriptorSets[renderContext.currentImageIndex]};

    vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 2, sets, 0, nullptr);

    const auto &fog = data.fogSettings;
    const auto quality = RenderQualitySettings::getInstance().volumetricFogQuality;

    FogLightingPC pc{};
    pc.fogColorDensity = glm::vec4(fog.color, std::max(0.0f, fog.density));
    pc.fogParams0 = glm::vec4(
        std::max(0.0f, fog.startDistance),
        std::clamp(fog.maxOpacity, 0.0f, 1.0f),
        fog.heightBase,
        std::max(0.0f, fog.heightFalloff));
    pc.fogParams1 = glm::vec4(
        std::clamp(fog.anisotropy, -0.95f, 0.95f),
        std::max(0.0f, fog.shaftIntensity),
        std::clamp(fog.dustAmount, 0.0f, 1.0f),
        std::max(0.0001f, fog.noiseScale));
    pc.fogParams2 = glm::vec4(
        std::max(0.0f, fog.noiseScrollSpeed),
        data.elapsedTime,
        quality == RenderQualitySettings::VolumetricFogQuality::High ? 1.0f : 0.0f,
        quality == RenderQualitySettings::VolumetricFogQuality::High ? 1.0f : 0.0f);
    pc.extentInfo = glm::vec4(
        static_cast<float>(m_fullExtent.width),
        static_cast<float>(m_fullExtent.height),
        static_cast<float>(m_extent.width),
        static_cast<float>(m_extent.height));

    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<IRenderGraphPass::RenderPassExecution>
VolumetricFogLightingRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
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
    exec.targets[m_outputHandlers[renderContext.currentImageIndex]] = m_outputTargets[renderContext.currentImageIndex];
    return {exec};
}

void VolumetricFogLightingRenderGraphPass::setExtent(VkExtent2D extent)
{
    if (m_fullExtent.width == extent.width && m_fullExtent.height == extent.height)
        return;

    m_fullExtent = extent;
    updateInternalExtent();
    requestRecompilation();
}

void VolumetricFogLightingRenderGraphPass::freeResources()
{
    m_outputHandlers.clear();
    m_outputTargets.clear();
    for (auto &descriptorSet : m_descriptorSets)
        descriptorSet = VK_NULL_HANDLE;
    m_descriptorSetsInitialized = false;
    outputs.color.set(MultiHandle{});
}

void VolumetricFogLightingRenderGraphPass::updateInternalExtent()
{
    m_extent = computeFogExtent(m_fullExtent);
    m_viewport = {0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    m_scissor = {{0, 0}, m_extent};
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
