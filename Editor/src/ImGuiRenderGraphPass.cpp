#include "Editor/ImGuiRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

ImGuiRenderGraphPass::ImGuiRenderGraphPass(std::shared_ptr<Editor> editor)
: m_editor(editor), m_device(core::VulkanContext::getContext()->getDevice())
{
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    m_clearValues[1].depthStencil = {1.0f, 0};
}

void ImGuiRenderGraphPass::setViewportImages(const std::vector<VkImageView>& imageViews)
{
    m_descriptorSets.clear();

    for(int index = 0; index < imageViews.size(); ++index)
    {
        m_descriptorSets.push_back(ImGui_ImplVulkan_AddTexture(m_sampler, imageViews[index], 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    }
}

void ImGuiRenderGraphPass::setup(engine::RenderGraphPassRecourceBuilder& graphPassBuilder)
{
    VkAttachmentDescription attachment = {};
    attachment.format = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
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

    VkAttachmentReference color_attachment = {};
    color_attachment.attachment = 0;
    color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0; // or VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    m_renderPass = core::RenderPass::create(core::VulkanContext::getContext()->getDevice(), {attachment}, {subpass}, {dependency});

    engine::RenderGraphPassResourceTypes::SizeSpec sizeSpec
    {
        .type = engine::RenderGraphPassResourceTypes::SizeClass::SwapchainRelative,
    };
    
    engine::RenderGraphPassResourceTypes::TextureDescription colorTexture{
        // .name = "__ELIX_SWAP_CHAIN_COLOR__",
        .name = "__ELIX_IMGUI_COLOR__",
        .size = sizeSpec,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
        .source = engine::RenderGraphPassResourceTypes::TextureDescription::FormatSource::Swapchain
    };

    auto colorImageHash = graphPassBuilder.createTexture(colorTexture);

    for(size_t i = 0; i < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++i)
    {
        const std::string name = "__ELIX_IMGUI_FRAMEBUFFER_" + std::to_string(i) + "__";

        engine::RenderGraphPassResourceTypes::FramebufferDescription framebufferDescription{
            .name = name,
            .attachmentsHash = {colorImageHash + i},
            .renderPass = m_renderPass,
            .size = sizeSpec,
            .layers = 1
        };

        m_framebufferHashes.push_back(graphPassBuilder.createFramebuffer(framebufferDescription));
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
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
}

void ImGuiRenderGraphPass::compile(engine::RenderGraphPassResourceHash& storage)
{
    for(const auto& framebufferCache : m_framebufferHashes)
    {
        auto framebuffer = storage.getFramebuffer(framebufferCache);

        if(!framebuffer)
        {
            std::cerr << "Failed to find a framebuffer for BaseRenderGraphPass" << std::endl;
            continue;
        }

        m_framebuffers.push_back(framebuffer);
    }

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

    VkDescriptorPoolSize poolSizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

	VkDescriptorPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = 1000;
	poolInfo.poolSizeCount = std::size(poolSizes);
	poolInfo.pPoolSizes = poolSizes;

	if(vkCreateDescriptorPool(core::VulkanContext::getContext()->getDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor pool");

    ImGui_ImplGlfw_InitForVulkan(core::VulkanContext::getContext()->getSwapchain()->getWindow()->getRawHandler(), true);

    ImGui_ImplVulkan_InitInfo imguiInitInfo{};
    imguiInitInfo.Instance = core::VulkanContext::getContext()->getInstance();
    imguiInitInfo.PhysicalDevice = core::VulkanContext::getContext()->getPhysicalDevice();
    imguiInitInfo.Device = core::VulkanContext::getContext()->getDevice();
    imguiInitInfo.QueueFamily = core::VulkanContext::getContext()->getQueueFamilyIndices().graphicsFamily.value();
    imguiInitInfo.Queue = core::VulkanContext::getContext()->getGraphicsQueue();
    imguiInitInfo.PipelineCache = VK_NULL_HANDLE;
    imguiInitInfo.DescriptorPool = m_descriptorPool;
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

void ImGuiRenderGraphPass::getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const
{
    renderPassBeginInfo = VkRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass = m_renderPass->vk();
    renderPassBeginInfo.framebuffer = m_framebuffers[m_currentImageIndex]->vk();
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();
    renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(m_clearValues.size());
    renderPassBeginInfo.pClearValues = m_clearValues.data();
}

void ImGuiRenderGraphPass::update(const engine::RenderGraphPassContext& renderData)
{
    m_currentFrame = renderData.currentFrame;
    m_currentImageIndex = renderData.currentImageIndex;
}

void ImGuiRenderGraphPass::execute(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassPerFrameData& data)
{
    if(data.isViewportImageViewsDirty)
        setViewportImages(data.viewportImageViews);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(core::VulkanContext::getContext()->getSwapchain()->getExtent().width);
    viewport.height = static_cast<float>(core::VulkanContext::getContext()->getSwapchain()->getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &scissor);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    m_editor->drawFrame(!m_descriptorSets.empty() && m_descriptorSets.size() > m_currentFrame ?
    m_descriptorSets.at(m_currentFrame) : VK_NULL_HANDLE);

    ImGui::Render();

    if (ImGuiIO& io = ImGui::GetIO(); io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer->vk());

    // ImGui_ImplVulkan_Shutdown();
    // ImGui_ImplGlfw_Shutdown();
    // ImGui::DestroyContext();
}

ELIX_NESTED_NAMESPACE_END