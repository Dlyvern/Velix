#include "Core/Framebuffer.hpp"
#include "Core/VulkanAssert.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)

Framebuffer::Framebuffer(VkDevice device, const std::vector<VkImageView> &attachments, core::RenderPass &renderPass, VkExtent2D extent, uint32_t layers)
    : m_device(device), m_attachments(attachments), m_renderPass(&renderPass), m_layers(layers), m_extent(extent)
{
    createVk(m_device, attachments, renderPass, extent, layers);
}

void Framebuffer::resize(VkExtent2D newExtent, const std::vector<VkImageView> &newAttachments)
{
    m_extent = newExtent;
    m_attachments = newAttachments;
    destroyVk();
    createVk(m_device, m_attachments, *m_renderPass, m_extent, m_layers);
}

void Framebuffer::createVk(VkDevice device, const std::vector<VkImageView> &attachments, core::RenderPass &renderPass, VkExtent2D extent, uint32_t layers)
{
    ELIX_VK_CREATE_GUARD()

    VkFramebufferCreateInfo framebufferCI{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebufferCI.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferCI.flags = 0;
    framebufferCI.pAttachments = attachments.data();
    framebufferCI.renderPass = renderPass.vk();
    framebufferCI.height = extent.height;
    framebufferCI.width = extent.width;
    framebufferCI.layers = layers;

    VX_VK_CHECK(vkCreateFramebuffer(device, &framebufferCI, nullptr, &m_handle));

    ELIX_VK_CREATE_GUARD_DONE()
}

const VkExtent2D &Framebuffer::getExtent() const
{
    return m_extent;
}

void Framebuffer::destroyVkImpl()
{
    if (m_handle)
    {
        vkDestroyFramebuffer(m_device, m_handle, nullptr);
        m_handle = VK_NULL_HANDLE;
    }
}

Framebuffer::~Framebuffer()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END
