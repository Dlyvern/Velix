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
    m_clearValues[1].depthStencil = {1.0f, 0};
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

void ImGuiRenderGraphPass::compile(engine::renderGraph::RGPResourcesStorage &storage)
{
    std::vector<VkImageView> offscreenImageViews;

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        auto colorTexture = storage.getSwapChainTexture(m_colorTextureHandler, imageIndex);
        std::vector<VkImageView> attachments{colorTexture->vkImageView()};

        auto framebuffer = core::Framebuffer::createShared(core::VulkanContext::getContext()->getDevice(), attachments,
                                                           m_renderPass, core::VulkanContext::getContext()->getSwapchain()->getExtent());

        m_framebuffers.push_back(framebuffer);

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
        auto frameBuffer = m_framebuffers[imageIndex];

        auto colorTexture = storage.getSwapChainTexture(m_colorTextureHandler, imageIndex);
        std::vector<VkImageView> attachments{colorTexture->vkImageView()};

        frameBuffer->resize(core::VulkanContext::getContext()->getSwapchain()->getExtent(), attachments);

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
    VkAttachmentDescription attachment = {};
    attachment.format = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // VkAttachmentDescription colorAttachment{};
    // colorAttachment.format = m_swapchain->getImageFormat();
    // colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    // colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    // colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachment = {};
    colorAttachment.attachment = 0;
    colorAttachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachment;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0; // or VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    m_renderPass = core::RenderPass::createShared(
        std::vector<VkAttachmentDescription>{attachment},
        std::vector<VkSubpassDescription>{subpass},
        std::vector<VkSubpassDependency>{dependency});

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
        throw std::runtime_error("failed to create texture sampler!");

    engine::renderGraph::RGPTextureDescription colorTextureDescription{};
    colorTextureDescription.setDebugName("__ELIX_IMGUI_COLOR__");
    colorTextureDescription.setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    colorTextureDescription.setFormat(core::VulkanContext::getContext()->getSwapchain()->getImageFormat());
    colorTextureDescription.setUsage(engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
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

    ImGui_ImplGlfw_InitForVulkan(core::VulkanContext::getContext()->getSwapchain()->getWindow()->getRawHandler(), true);

    ImGui_ImplVulkan_InitInfo imguiInitInfo{};
    imguiInitInfo.Instance = core::VulkanContext::getContext()->getInstance();
    imguiInitInfo.PhysicalDevice = core::VulkanContext::getContext()->getPhysicalDevice();
    imguiInitInfo.Device = core::VulkanContext::getContext()->getDevice();
    imguiInitInfo.QueueFamily = core::VulkanContext::getContext()->getGraphicsFamily();
    imguiInitInfo.Queue = core::VulkanContext::getContext()->getGraphicsQueue();
    imguiInitInfo.PipelineCache = VK_NULL_HANDLE;
    imguiInitInfo.DescriptorPoolSize = 1000;
    imguiInitInfo.MinImageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    imguiInitInfo.ImageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    imguiInitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    imguiInitInfo.RenderPass = m_renderPass->vk();
    imguiInitInfo.Subpass = 0;
    imguiInitInfo.CheckVkResultFn = nullptr;
    imguiInitInfo.Allocator = nullptr;

    ImGui_ImplVulkan_Init(&imguiInitInfo);

    m_editor->initStyle();
}

void ImGuiRenderGraphPass::getRenderPassBeginInfo(VkRenderPassBeginInfo &renderPassBeginInfo) const
{
    const float width = m_editor->getViewportX();
    const float height = m_editor->getViewportY();

    VkExtent2D extent{
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
    };

    renderPassBeginInfo = VkRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass = m_renderPass->vk();
    renderPassBeginInfo.framebuffer = m_framebuffers[m_currentImageIndex]->vk();
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = extent;
    renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(m_clearValues.size());
    renderPassBeginInfo.pClearValues = m_clearValues.data();
}

void ImGuiRenderGraphPass::update(const engine::RenderGraphPassContext &renderData)
{
    m_currentFrame = renderData.currentFrame;
    m_currentImageIndex = renderData.currentImageIndex;
}

void ImGuiRenderGraphPass::execute(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassPerFrameData &data)
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

    m_editor->drawFrame(!m_descriptorSets.empty() && m_descriptorSets.size() > m_currentFrame ? m_descriptorSets.at(m_currentFrame) : VK_NULL_HANDLE);

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