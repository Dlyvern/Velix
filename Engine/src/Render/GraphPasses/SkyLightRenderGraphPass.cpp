#include "Engine/Render/GraphPasses/SkyLightRenderGraphPass.hpp"

#include "Core/VulkanHelpers.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

SkyLightRenderGraphPass::SkyLightRenderGraphPass(uint32_t lightingId, uint32_t gbufferId,
                                                 std::vector<RGPResourceHandler> &lightingInputHandlers,
                                                 RGPResourceHandler &depthTextureHandler)
    : m_lightingInputHandlers(lightingInputHandlers),
      m_depthTextureHandler(depthTextureHandler)
{
    setDebugName("Sky light render graph pass");
    addDependOnRenderGraphPass(lightingId);
    addDependOnRenderGraphPass(gbufferId);

    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};

    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void SkyLightRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                                     const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    GraphicsPipelineKey copyKey{};
    copyKey.shader = ShaderId::Present;
    copyKey.cull = CullMode::None;
    copyKey.depthTest = false;
    copyKey.depthWrite = false;
    copyKey.depthCompare = VK_COMPARE_OP_ALWAYS;
    copyKey.polygonMode = VK_POLYGON_MODE_FILL;
    copyKey.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    copyKey.colorFormats = {m_colorFormat};
    copyKey.depthFormat = m_depthFormat;
    copyKey.pipelineLayout = m_copyPipelineLayout;

    auto copyPipeline = GraphicsPipelineManager::getOrCreate(copyKey);

    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, copyPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_copyPipelineLayout, 0, 1,
                            &m_copyDescriptorSets[renderContext.currentImageIndex], 0, nullptr);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);

    auto skyKey = m_skyLightSystem->getGraphicsPipelineKey();
    skyKey.colorFormats = {m_colorFormat};
    skyKey.depthFormat = m_depthFormat;

    auto skyPipeline = GraphicsPipelineManager::getOrCreate(skyKey);

    m_skyLightSystem->setSunDirection(-data.directionalLightDirection);
    m_skyLightSystem->render(commandBuffer, data.directionalLightStrength, data.deltaTime, data.view, data.projection, skyPipeline);
}

std::vector<IRenderGraphPass::RenderPassExecution> SkyLightRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution renderPassExecution{};
    renderPassExecution.renderArea.offset = {0, 0};
    renderPassExecution.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_colorRenderTargets[renderContext.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = m_clearValues[0];

    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = m_depthRenderTarget->vkImageView();
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue = {.depthStencil = {1.0f, 0}};

    renderPassExecution.colorsRenderingItems = {color};
    renderPassExecution.depthRenderingItem = depth;
    renderPassExecution.useDepth = true;
    renderPassExecution.depthFormat = m_depthFormat;
    renderPassExecution.colorFormats = {m_colorFormat};

    renderPassExecution.targets[m_colorTextureHandler[renderContext.currentImageIndex]] = m_colorRenderTargets[renderContext.currentImageIndex];
    renderPassExecution.targets[m_depthTextureHandler] = m_depthRenderTarget;

    return {renderPassExecution};
}

void SkyLightRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = VkViewport{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    m_scissor = VkRect2D{VkOffset2D{0, 0}, m_extent};
    requestRecompilation();
}

void SkyLightRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    m_colorRenderTargets.resize(imageCount);
    m_copyDescriptorSets.resize(imageCount);
    m_depthRenderTarget = storage.getTexture(m_depthTextureHandler);

    static bool wasBuilt{false};

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_colorRenderTargets[i] = storage.getTexture(m_colorTextureHandler[i]);
        auto lightingTexture = storage.getTexture(m_lightingInputHandlers[i]);

        if (!wasBuilt)
        {
            m_copyDescriptorSets[i] = DescriptorSetBuilder::begin()
                                          .addImage(lightingTexture->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                          .build(core::VulkanContext::getContext()->getDevice(),
                                                 core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                                 m_copyDescriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(lightingTexture->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .update(core::VulkanContext::getContext()->getDevice(), m_copyDescriptorSets[i]);
        }
    }

    if (!wasBuilt)
        wasBuilt = true;
}

void SkyLightRenderGraphPass::setup(RGPResourcesBuilder &builder)
{
    m_colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    m_depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());

    for (auto &input : m_lightingInputHandlers)
        builder.read(input, RGPTextureUsage::SAMPLED);

    builder.read(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);

    RGPTextureDescription colorTextureDescription{m_colorFormat, RGPTextureUsage::COLOR_ATTACHMENT};
    colorTextureDescription.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    colorTextureDescription.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    colorTextureDescription.setCustomExtentFunction([this]
                                                    { return m_extent; });

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        colorTextureDescription.setDebugName("__ELIX_COLOR_SKY_LIGHT_TEXTURE_" + std::to_string(imageIndex) + "__");
        auto colorTexture = builder.createTexture(colorTextureDescription);
        m_colorTextureHandler.push_back(colorTexture);
        builder.write(colorTexture, RGPTextureUsage::COLOR_ATTACHMENT);
    }

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.pImmutableSamplers = nullptr;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_copyDescriptorSetLayout = core::DescriptorSetLayout::createShared(device, std::vector<VkDescriptorSetLayoutBinding>{binding});
    m_copyPipelineLayout = core::PipelineLayout::createShared(device, std::vector<core::DescriptorSetLayout::SharedPtr>{m_copyDescriptorSetLayout},
                                                              std::vector<VkPushConstantRange>{});

    m_defaultSampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    if (!m_skyLightSystem)
        m_skyLightSystem = std::make_unique<SkyLightSystem>();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
