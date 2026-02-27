#include "Engine/Render/GraphPasses/SkyLightRenderGraphPass.hpp"

#include "Core/VulkanHelpers.hpp"
#include "Core/VulkanContext.hpp"

#include "Engine/Builders/GraphicsPipelineManager.hpp"

#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

SkyLightRenderGraphPass::SkyLightRenderGraphPass(std::vector<RGPResourceHandler> &lightingInputHandlers,
                                                 RGPResourceHandler &depthTextureHandler)
    : m_lightingInputHandlers(lightingInputHandlers),
      m_depthTextureHandler(depthTextureHandler)
{
    setDebugName("Sky light render graph pass");

    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void SkyLightRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                                     const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    if (m_loadedSkyboxHDRPath != data.skyboxHDRPath)
    {
        m_skybox.reset();
        m_loadedSkyboxHDRPath.clear();

        if (!data.skyboxHDRPath.empty())
        {
            if (!std::filesystem::exists(data.skyboxHDRPath))
            {
                VX_ENGINE_WARNING_STREAM("Skybox HDR file was not found: " << data.skyboxHDRPath << '\n');
                m_loadedSkyboxHDRPath = data.skyboxHDRPath;
            }
            else
            {
                m_skybox = std::make_unique<Skybox>(data.skyboxHDRPath, core::VulkanContext::getContext()->getPersistentDescriptorPool()->vk());
                m_loadedSkyboxHDRPath = data.skyboxHDRPath;
                VX_ENGINE_INFO_STREAM("Loaded skybox HDR: " << m_loadedSkyboxHDRPath << '\n');
            }
        }
    }

    if (m_skybox)
    {
        auto skyKey = m_skybox->getGraphicsPipelineKey();
        skyKey.colorFormats = {m_colorFormat};
        skyKey.depthFormat = m_depthFormat;

        auto skyPipeline = GraphicsPipelineManager::getOrCreate(skyKey);
        m_skybox->render(commandBuffer, data.view, data.projection, skyPipeline);
        return;
    }

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
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

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
    m_depthRenderTarget = storage.getTexture(m_depthTextureHandler);

    for (uint32_t i = 0; i < imageCount; ++i)
        m_colorRenderTargets[i] = storage.getTexture(m_colorTextureHandler[i]);
}

void SkyLightRenderGraphPass::setup(RGPResourcesBuilder &builder)
{
    m_colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    m_depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());

    m_colorTextureHandler.clear();
    m_colorTextureHandler.reserve(m_lightingInputHandlers.size());

    for (const auto &input : m_lightingInputHandlers)
    {
        builder.write(input, RGPTextureUsage::COLOR_ATTACHMENT);
        m_colorTextureHandler.push_back(input);
    }

    builder.read(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);

    if (!m_skyLightSystem)
        m_skyLightSystem = std::make_unique<SkyLightSystem>();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
