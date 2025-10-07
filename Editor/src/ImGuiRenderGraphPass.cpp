#include "Editor/ImGuiRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <stdexcept>
//*Proxies should have another proxies and create them with default names
//*For example, SwapChain proxy can create RenderPassProxy with default name 'SwapChainRenderPass' and the same with framebuffers etc...

ELIX_NESTED_NAMESPACE_BEGIN(editor)

ImGuiRenderGraphPass::ImGuiRenderGraphPass(std::shared_ptr<Editor> editor) : m_editor(editor)
{
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    m_clearValues[1].depthStencil = {1.0f, 0};
}

void ImGuiRenderGraphPass::setup(std::shared_ptr<engine::RenderGraphPassBuilder> builder)
{
    m_swapChainProxy = builder->createProxy<engine::SwapChainRenderGraphProxy>("__ELIX_SWAP_CHAIN_PROXY__");
    m_swapChainProxy->isDependedOnSwapChain = true;
    m_swapChainProxy->addOnSwapChainRecretedFunction([this]
    {
        for(auto& framebuffer : m_framebuffers)
            if(framebuffer)
                vkDestroyFramebuffer(core::VulkanContext::getContext()->getDevice(), framebuffer, nullptr);

        createFramebuffers();
    });
}

void ImGuiRenderGraphPass::createFramebuffers()
{
    for (uint32_t i = 0; i < m_swapChainProxy->imageViews.size(); i++)
    {
        std::vector<VkImageView> framebufferAttachment{m_swapChainProxy->imageViews[i]};
        VkFramebufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = m_renderPass->vk();
        info.attachmentCount = static_cast<uint32_t>(framebufferAttachment.size());
        info.pAttachments = framebufferAttachment.data();
        info.width = core::VulkanContext::getContext()->getSwapchain()->getExtent().width;
        info.height = core::VulkanContext::getContext()->getSwapchain()->getExtent().height;
        info.layers = 1;

        if (vkCreateFramebuffer(core::VulkanContext::getContext()->getDevice(), &info, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to create framebuffer!");
    }
}

void ImGuiRenderGraphPass::compile()
{
    VkAttachmentDescription attachment = {};
    attachment.format = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    //TODO MAYBE THIS IS NEEDED
    // attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

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
    
    m_framebuffers.resize(m_swapChainProxy->imageViews.size());

    createFramebuffers();

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

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
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
}

void ImGuiRenderGraphPass::getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const
{
    renderPassBeginInfo = VkRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass = m_renderPass->vk();
    renderPassBeginInfo.framebuffer = m_framebuffers[m_currentImageIndex];
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();
    renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(m_clearValues.size());
    renderPassBeginInfo.pClearValues = m_clearValues.data();
}

void ImGuiRenderGraphPass::update(uint32_t currentFrame, uint32_t currentImageIndex, VkFramebuffer fr)
{
    m_currentFramebuffer = fr;
    m_currentImageIndex = currentImageIndex;
}

void ImGuiRenderGraphPass::execute(core::CommandBuffer::SharedPtr commandBuffer)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    m_editor->drawFrame();

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