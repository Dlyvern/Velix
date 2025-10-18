#include "Core/Framebuffer.hpp"
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

Framebuffer::Framebuffer(VkDevice device, const std::vector<VkImageView>& attachments, core::RenderPass::SharedPtr renderPass, VkExtent2D extent, uint32_t layers) 
: m_device(device), m_attachments(attachments), m_renderPass(renderPass), m_layers(layers), m_extent(extent)
{
    createVk(m_device, attachments, renderPass, extent, layers);
}

void Framebuffer::resize(VkExtent2D newExtent, const std::vector<VkImageView>& newAttachments)
{
    m_extent = newExtent;
    m_attachments = newAttachments;
    destroyVk();
    createVk(m_device, m_attachments, m_renderPass.lock(), m_extent, m_layers);
}

void Framebuffer::createVk(VkDevice device, const std::vector<VkImageView>& attachments, core::RenderPass::SharedPtr renderPass, VkExtent2D extent, uint32_t layers)
{
    VkFramebufferCreateInfo framebufferCI{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebufferCI.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferCI.flags = 0;
    framebufferCI.pAttachments = attachments.data();
    framebufferCI.renderPass = renderPass->vk();
    framebufferCI.height = extent.height;
    framebufferCI.width = extent.width;
    framebufferCI.layers = layers;

    if(vkCreateFramebuffer(device, &framebufferCI, nullptr, &m_framebuffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to create framebuffer");
}

const VkExtent2D& Framebuffer::getExtent() const
{
    return m_extent;
}

void Framebuffer::destroyVk()
{
    if(m_framebuffer)
    {
        vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
}

VkFramebuffer Framebuffer::vk() const
{
    return m_framebuffer;
}

Framebuffer::~Framebuffer()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END