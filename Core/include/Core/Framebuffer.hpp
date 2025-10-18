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
public:
    using SharedPtr = std::shared_ptr<Framebuffer>;

    Framebuffer(VkDevice device, const std::vector<VkImageView>& attachments, core::RenderPass::SharedPtr renderPass, VkExtent2D extent, uint32_t layers = 1);

    ~Framebuffer();

    VkFramebuffer vk() const;

    const VkExtent2D& getExtent() const;
    void resize(VkExtent2D newExtent, const std::vector<VkImageView>& newAttachments);
    void createVk(VkDevice device, const std::vector<VkImageView>& attachments, core::RenderPass::SharedPtr renderPass, VkExtent2D extent, uint32_t layers = 1);
    void destroyVk();
private:
    VkFramebuffer m_framebuffer{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    std::vector<VkImageView> m_attachments;
    std::weak_ptr<core::RenderPass> m_renderPass;
    uint32_t m_layers;
    VkExtent2D m_extent;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_FRAMEBUFFER_HPP