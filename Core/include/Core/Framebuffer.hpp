#ifndef ELIX_FRAMEBUFFER_HPP
#define ELIX_FRAMEBUFFER_HPP

#include "Core/Macros.hpp"
#include "Core/RenderPass.hpp"

#include <volk.h>

#include <vector>
#include <cstdint>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class Framebuffer
{
    DECLARE_VK_HANDLE_METHODS(VkFramebuffer)
    DECLARE_VK_SMART_PTRS(Framebuffer, VkFramebuffer)
    ELIX_DECLARE_VK_LIFECYCLE()
public:
    Framebuffer(VkDevice device, const std::vector<VkImageView> &attachments, core::RenderPass &renderPass, VkExtent2D extent, uint32_t layers = 1);

    ~Framebuffer();

    const VkExtent2D &getExtent() const;
    void resize(VkExtent2D newExtent, const std::vector<VkImageView> &newAttachments);
    void createVk(VkDevice device, const std::vector<VkImageView> &attachments, core::RenderPass &renderPass, VkExtent2D extent, uint32_t layers = 1);

private:
    VkDevice m_device{VK_NULL_HANDLE};
    std::vector<VkImageView> m_attachments;
    core::RenderPass *m_renderPass{nullptr};
    uint32_t m_layers;
    VkExtent2D m_extent;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_FRAMEBUFFER_HPP
