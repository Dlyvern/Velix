#include "Core/RenderPass.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Core/VulkanContext.hpp"

#include <stdexcept>
#include <array>

ELIX_NESTED_NAMESPACE_BEGIN(core)

RenderPass::RenderPass(const std::vector<VkAttachmentDescription> &attachments, const std::vector<VkSubpassDescription> &subpasses,
                       const std::vector<VkSubpassDependency> &dependencies) : m_device(core::VulkanContext::getContext()->getDevice())
{
    VkRenderPassCreateInfo renderPassCI{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassCI.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassCI.pAttachments = attachments.data();
    renderPassCI.subpassCount = static_cast<uint32_t>(subpasses.size());
    renderPassCI.pSubpasses = subpasses.data();
    renderPassCI.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassCI.pDependencies = dependencies.data();

    if (vkCreateRenderPass(m_device, &renderPassCI, nullptr, &m_renderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create render pass");
}

RenderPass::SharedPtr RenderPass::create(const std::vector<VkAttachmentDescription> &attachments,
                                         const std::vector<VkSubpassDescription> &subpasses, const std::vector<VkSubpassDependency> &dependencies)
{
    return std::make_shared<RenderPass>(attachments, subpasses, dependencies);
}

VkRenderPass RenderPass::vk()
{
    return m_renderPass;
}

RenderPass::~RenderPass()
{
    if (m_renderPass)
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
}

ELIX_NESTED_NAMESPACE_END