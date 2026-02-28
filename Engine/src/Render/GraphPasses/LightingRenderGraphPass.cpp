#include "Engine/Render/GraphPasses/LightingRenderGraphPass.hpp"

#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

LightingRenderGraphPass::LightingRenderGraphPass(RGPResourceHandler &shadowTextureHandler,
                                                 RGPResourceHandler &depthTextureHandler, RGPResourceHandler &cubeTextureHandler, RGPResourceHandler &arrayTextureHandler,
                                                 std::vector<RGPResourceHandler> &albedoTextureHandlers,
                                                 std::vector<RGPResourceHandler> &normalTextureHandlers,
                                                 std::vector<RGPResourceHandler> &materialTextureHandlers,
                                                 std::vector<RGPResourceHandler> *aoTextureHandlers)
    : m_albedoTextureHandlers(albedoTextureHandlers),
      m_normalTextureHandlers(normalTextureHandlers),
      m_materialTextureHandlers(materialTextureHandlers),
      m_depthTextureHandler(depthTextureHandler),
      m_shadowTextureHandler(shadowTextureHandler),
      m_cubeTextureHandler(cubeTextureHandler),
      m_arrayTextureHandler(arrayTextureHandler),
      m_aoTextureHandlers(aoTextureHandlers)
{
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};

    this->setDebugName("Lighting render graph pass");

    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void LightingRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                                     const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    GraphicsPipelineKey key{};
    key.shader = ShaderId::Lighting;
    key.cull = CullMode::None;
    key.depthTest = false;
    key.depthWrite = false;
    key.depthCompare = VK_COMPARE_OP_LESS;
    key.polygonMode = VK_POLYGON_MODE_FILL;
    key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats = {m_colorFormat};
    key.pipelineLayout = m_pipelineLayout;

    auto graphicsPipeline = GraphicsPipelineManager::getOrCreate(key);

    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    std::vector<VkDescriptorSet> descriptorSets{
        data.cameraDescriptorSet, // set 0: camera & light
        m_descriptorSets[renderContext.currentImageIndex]};

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, static_cast<uint32_t>(descriptorSets.size()),
                            descriptorSets.data(), 0, nullptr);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<IRenderGraphPass::RenderPassExecution> LightingRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution renderPassExecution;
    renderPassExecution.renderArea.offset = {0, 0};
    renderPassExecution.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_colorRenderTargets[renderContext.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = m_clearValues[0];

    renderPassExecution.colorsRenderingItems = {color};
    renderPassExecution.useDepth = false;
    renderPassExecution.depthFormat = VK_FORMAT_UNDEFINED;

    renderPassExecution.colorFormats = {m_colorFormat};

    renderPassExecution.targets[m_colorTextureHandler[renderContext.currentImageIndex]] = m_colorRenderTargets[renderContext.currentImageIndex];

    return {renderPassExecution};
}

void LightingRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = VkViewport{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    m_scissor = VkRect2D{VkOffset2D{0, 0}, m_extent};
    requestRecompilation();
}

void LightingRenderGraphPass::compile(RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    m_colorRenderTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_colorRenderTargets[i] = storage.getTexture(m_colorTextureHandler[i]);

        auto normalTexture = storage.getTexture(m_normalTextureHandlers[i]);
        auto albedoTexture = storage.getTexture(m_albedoTextureHandlers[i]);
        auto materialTexture = storage.getTexture(m_materialTextureHandlers[i]);
        auto depthTexture = storage.getTexture(m_depthTextureHandler);
        auto shadowTexture = storage.getTexture(m_shadowTextureHandler);
        auto cubeTexture = storage.getTexture(m_cubeTextureHandler);
        auto arrayTexture = storage.getTexture(m_arrayTextureHandler);

        // AO texture: use the SSAO output when available, otherwise fall back to depth (shader ignores it)
        const RenderTarget *aoTexture = depthTexture;
        VkImageLayout aoLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        if (m_aoTextureHandlers && i < m_aoTextureHandlers->size())
        {
            aoTexture = storage.getTexture((*m_aoTextureHandlers)[i]);
            aoLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(normalTexture->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .addImage(albedoTexture->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                                      .addImage(materialTexture->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                                      .addImage(depthTexture->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 3)
                                      .addImage(shadowTexture->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 4)
                                      .addImage(arrayTexture->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 5)
                                      .addImage(cubeTexture->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 6)
                                      .addImage(aoTexture->vkImageView(), m_defaultSampler, aoLayout, 7)
                                      .build(core::VulkanContext::getContext()->getDevice(), core::VulkanContext::getContext()->getPersistentDescriptorPool(), m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(normalTexture->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(albedoTexture->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .addImage(materialTexture->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                .addImage(depthTexture->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 3)
                .addImage(shadowTexture->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 4)
                .addImage(arrayTexture->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 5)
                .addImage(cubeTexture->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 6)
                .addImage(aoTexture->vkImageView(), m_defaultSampler, aoLayout, 7)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void LightingRenderGraphPass::setup(RGPResourcesBuilder &builder)
{
    auto lightPassFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    m_colorFormat = lightPassFormat;

    RGPTextureDescription colorTextureDescription{lightPassFormat, RGPTextureUsage::COLOR_ATTACHMENT};

    colorTextureDescription.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    colorTextureDescription.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    colorTextureDescription.setCustomExtentFunction([this]
                                                    { return m_extent; });

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        colorTextureDescription.setDebugName("__ELIX_COLOR_LIGHTING_TEXTURE_" + std::to_string(imageIndex) + "__");
        auto colorTexture = builder.createTexture(colorTextureDescription);
        m_colorTextureHandler.push_back(colorTexture);
        builder.write(colorTexture, RGPTextureUsage::COLOR_ATTACHMENT);
        builder.read(m_albedoTextureHandlers[imageIndex], RGPTextureUsage::SAMPLED);
        builder.read(m_normalTextureHandlers[imageIndex], RGPTextureUsage::SAMPLED);
        builder.read(m_materialTextureHandlers[imageIndex], RGPTextureUsage::SAMPLED);

        if (m_aoTextureHandlers && imageIndex < static_cast<int>(m_aoTextureHandlers->size()))
            builder.read((*m_aoTextureHandlers)[imageIndex], RGPTextureUsage::SAMPLED);
    }

    builder.read(m_depthTextureHandler, RGPTextureUsage::SAMPLED);
    builder.read(m_shadowTextureHandler, RGPTextureUsage::SAMPLED);
    builder.read(m_arrayTextureHandler, RGPTextureUsage::SAMPLED);
    builder.read(m_cubeTextureHandler, RGPTextureUsage::SAMPLED);

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding bindingNormal{};
    bindingNormal.binding = 0;
    bindingNormal.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindingNormal.descriptorCount = 1;
    bindingNormal.pImmutableSamplers = nullptr;
    bindingNormal.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding bindingAlbedo{};
    bindingAlbedo.binding = 1;
    bindingAlbedo.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindingAlbedo.descriptorCount = 1;
    bindingAlbedo.pImmutableSamplers = nullptr;
    bindingAlbedo.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding bindingMaterial{};
    bindingMaterial.binding = 2;
    bindingMaterial.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindingMaterial.descriptorCount = 1;
    bindingMaterial.pImmutableSamplers = nullptr;
    bindingMaterial.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding bindingDepth{};
    bindingDepth.binding = 3;
    bindingDepth.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindingDepth.descriptorCount = 1;
    bindingDepth.pImmutableSamplers = nullptr;
    bindingDepth.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding lightMapBinding{};
    lightMapBinding.binding = 4;
    lightMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    lightMapBinding.descriptorCount = 1;
    lightMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    lightMapBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding spotMapBinding{};
    spotMapBinding.binding = 5;
    spotMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    spotMapBinding.descriptorCount = 1;
    spotMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    spotMapBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding pointMapBinding{};
    pointMapBinding.binding = 6;
    pointMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pointMapBinding.descriptorCount = 1;
    pointMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pointMapBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding aoBinding{};
    aoBinding.binding = 7;
    aoBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    aoBinding.descriptorCount = 1;
    aoBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    aoBinding.pImmutableSamplers = nullptr;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(device, std::vector<VkDescriptorSetLayoutBinding>{bindingNormal,
                                                                                                                      bindingAlbedo, bindingMaterial, bindingDepth, lightMapBinding,
                                                                                                                      spotMapBinding, pointMapBinding, aoBinding});

    m_pipelineLayout = core::PipelineLayout::createShared(device,
                                                          std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*EngineShaderFamilies::cameraDescriptorSetLayout, *m_descriptorSetLayout},
                                                          std::vector<VkPushConstantRange>{});

    m_defaultSampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                            VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE, VK_COMPARE_OP_ALWAYS, VK_SAMPLER_MIPMAP_MODE_LINEAR);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
