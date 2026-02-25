#include "Core/RenderPass.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/VulkanAssert.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)

RenderPass::RenderPass(const std::vector<VkAttachmentDescription> &attachments, const std::vector<VkSubpassDescription> &subpasses,
                       const std::vector<VkSubpassDependency> &dependencies)
{
    createVk(attachments, subpasses, dependencies);
}

void RenderPass::createVk(const std::vector<VkAttachmentDescription> &attachments, const std::vector<VkSubpassDescription> &subpasses,
                          const std::vector<VkSubpassDependency> &dependencies)
{
    ELIX_VK_CREATE_GUARD()

    const auto &device = VulkanContext::getContext()->getDevice();

    VkRenderPassCreateInfo renderPassCI{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassCI.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassCI.pAttachments = attachments.data();
    renderPassCI.subpassCount = static_cast<uint32_t>(subpasses.size());
    renderPassCI.pSubpasses = subpasses.data();
    renderPassCI.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassCI.pDependencies = dependencies.data();

    VX_VK_CHECK(vkCreateRenderPass(device, &renderPassCI, nullptr, &m_handle));

    ELIX_VK_CREATE_GUARD_DONE()
}

void RenderPass::destroyVkImpl()
{
    if (m_handle)
    {
        const auto &device = VulkanContext::getContext()->getDevice();
        vkDestroyRenderPass(device, m_handle, nullptr);
        m_handle = nullptr;
    }
}

RenderPass::~RenderPass()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END
