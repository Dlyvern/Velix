#include "Editor/ImGuiRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

ImGuiRenderGraphPass::ImGuiRenderGraphPass(std::shared_ptr<Editor> editor, std::vector<engine::renderGraph::RGPResourceHandler> &offscreenTexture,
                                           engine::renderGraph::RGPResourceHandler &objectIdTextureHandler)
    : m_editor(editor), m_device(core::VulkanContext::getContext()->getDevice()), m_offscreenTextureHandler(offscreenTexture),
      m_objectIdTextureHandler(objectIdTextureHandler)
{
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    this->setDebugName("ImGui render graph pass");
}

void ImGuiRenderGraphPass::setViewportImages(const std::vector<VkImageView> &imageViews)
{
    m_descriptorSets.clear();

    for (int index = 0; index < imageViews.size(); ++index)
    {
        m_descriptorSets.push_back(ImGui_ImplVulkan_AddTexture(m_sampler, imageViews[index],
                                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    }
}

void ImGuiRenderGraphPass::endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassContext &context)
{
    m_colorRenderTargets[context.currentImageIndex]->getImage()->insertImageMemoryBarrier(
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        0,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, *commandBuffer);
}

void ImGuiRenderGraphPass::startBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassContext &context)
{
    m_colorRenderTargets[context.currentImageIndex]->getImage()->insertImageMemoryBarrier(
        0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, *commandBuffer);
}

void ImGuiRenderGraphPass::compile(engine::renderGraph::RGPResourcesStorage &storage)
{
    std::vector<VkImageView> offscreenImageViews;

    m_colorRenderTargets.resize(core::VulkanContext::getContext()->getSwapchain()->getImages().size());

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        m_colorRenderTargets[imageIndex] = storage.getSwapChainTexture(m_colorTextureHandler, imageIndex);

        if (m_offscreenTextureHandler.size() > imageIndex)
        {
            auto offscreenColorTexture = storage.getTexture(m_offscreenTextureHandler[imageIndex]);

            if (offscreenColorTexture)
                offscreenImageViews.push_back(offscreenColorTexture->vkImageView());
            else
                std::cerr << "Failed to find offscreen color texture\n";
        }
        else
            std::cerr << "Something wrong with offscreen color texture size\n";
    }

    initImGui();

    setViewportImages(offscreenImageViews);

    auto objectIdTexture = storage.getTexture(m_objectIdTextureHandler);

    if (!objectIdTexture)
        std::cerr << "Failed to get object id texture\n";
    else
        m_editor->setObjectIdColorImage(objectIdTexture);
}

void ImGuiRenderGraphPass::onSwapChainResized(engine::renderGraph::RGPResourcesStorage &storage)
{
    std::vector<VkImageView> offscreenImageViews;

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        m_colorRenderTargets[imageIndex] = storage.getSwapChainTexture(m_colorTextureHandler, imageIndex);

        if (m_offscreenTextureHandler.size() > imageIndex)
        {
            auto offscreenColorTexture = storage.getTexture(m_offscreenTextureHandler[imageIndex]);

            if (offscreenColorTexture)
                offscreenImageViews.push_back(offscreenColorTexture->vkImageView());
            else
                std::cerr << "Failed to find offscreen color texture\n";
        }
        else
            std::cerr << "Something wrong with offscreen color texture size\n";
    }

    setViewportImages(offscreenImageViews);

    auto objectIdTexture = storage.getTexture(m_objectIdTextureHandler);

    if (!objectIdTexture)
        std::cerr << "Failed to get object id texture\n";
    else
        m_editor->setObjectIdColorImage(objectIdTexture);
}

void ImGuiRenderGraphPass::setup(engine::renderGraph::RGPResourcesBuilder &builder)
{
    m_colorFormat = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();

    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                            VK_BORDER_COLOR_INT_OPAQUE_BLACK, VK_COMPARE_OP_ALWAYS, VK_SAMPLER_MIPMAP_MODE_LINEAR);

    engine::renderGraph::RGPTextureDescription colorTextureDescription{m_colorFormat, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT};
    colorTextureDescription.setDebugName("__ELIX_IMGUI_COLOR__");
    colorTextureDescription.setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    colorTextureDescription.setIsSwapChainTarget(true);

    m_colorTextureHandler = builder.createTexture(colorTextureDescription);
    builder.write(m_colorTextureHandler, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);

    for (const auto &color : m_offscreenTextureHandler)
        builder.read(color, engine::renderGraph::RGPTextureUsage::SAMPLED);

    builder.read(m_objectIdTextureHandler, engine::renderGraph::RGPTextureUsage::SAMPLED);
}

void ImGuiRenderGraphPass::initImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    io.ConfigDockingWithShift = true;
    io.ConfigWindowsResizeFromEdges = true;
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    ImGuiStyle &style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000; // big enough for all textures
    pool_info.poolSizeCount = std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    vkCreateDescriptorPool(core::VulkanContext::getContext()->getDevice(), &pool_info, nullptr, &m_imguiDescriptorPool);

    ImGui_ImplGlfw_InitForVulkan(core::VulkanContext::getContext()->getSwapchain()->getWindow()->getRawHandler(), true);

    ImGui_ImplVulkan_InitInfo imguiInitInfo{};
    imguiInitInfo.Instance = core::VulkanContext::getContext()->getInstance();
    imguiInitInfo.PhysicalDevice = core::VulkanContext::getContext()->getPhysicalDevice();
    imguiInitInfo.Device = core::VulkanContext::getContext()->getDevice();
    imguiInitInfo.QueueFamily = core::VulkanContext::getContext()->getGraphicsFamily();
    imguiInitInfo.Queue = core::VulkanContext::getContext()->getGraphicsQueue();
    imguiInitInfo.PipelineCache = VK_NULL_HANDLE;
    imguiInitInfo.DescriptorPool = m_imguiDescriptorPool;
    imguiInitInfo.MinImageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    imguiInitInfo.ImageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    imguiInitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    imguiInitInfo.Subpass = 0;
    imguiInitInfo.CheckVkResultFn = nullptr;
    imguiInitInfo.Allocator = nullptr;

    imguiInitInfo.UseDynamicRendering = true;
    imguiInitInfo.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    imguiInitInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;

    VkFormat format = m_colorFormat;
    imguiInitInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;

    imguiInitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&imguiInitInfo);

    m_editor->initStyle();
}

std::vector<engine::renderGraph::IRenderGraphPass::RenderPassExecution> ImGuiRenderGraphPass::getRenderPassExecutions(const engine::RenderGraphPassContext &renderContext) const
{
    const float width = m_editor->getViewportX();
    const float height = m_editor->getViewportY();

    VkExtent2D extent{
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
    };

    IRenderGraphPass::RenderPassExecution renderPassExecution;
    renderPassExecution.renderArea.offset = {0, 0};
    renderPassExecution.renderArea.extent = extent;

    VkRenderingAttachmentInfo color0{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color0.imageView = m_colorRenderTargets[renderContext.currentImageIndex]->vkImageView();
    color0.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color0.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color0.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color0.clearValue = m_clearValues[0];

    renderPassExecution.colorsRenderingItems = {color0};
    renderPassExecution.useDepth = false;

    renderPassExecution.colorFormats = {m_colorFormat};
    renderPassExecution.depthFormat = VK_FORMAT_UNDEFINED;

    return {renderPassExecution};
}

void ImGuiRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassPerFrameData &data,
                                  const engine::RenderGraphPassContext &renderContext)
{
    const float width = m_editor->getViewportX();
    const float height = m_editor->getViewportY();

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &viewport);

    VkExtent2D extent{
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
    };

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &scissor);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    m_editor->drawFrame(!m_descriptorSets.empty() && m_descriptorSets.size() > renderContext.currentImageIndex ? m_descriptorSets.at(renderContext.currentImageIndex) : VK_NULL_HANDLE);

    ImGui::Render();

    if (ImGuiIO &io = ImGui::GetIO(); io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer->vk());
}

void ImGuiRenderGraphPass::cleanup()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

ELIX_NESTED_NAMESPACE_END