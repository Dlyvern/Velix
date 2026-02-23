#ifndef ELIX_RENDER_TARGET_HPP
#define ELIX_RENDER_TARGET_HPP

#include "Core/Macros.hpp"
#include "Core/Image.hpp"
#include "Core/Memory/MemoryFlags.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

// VkImage + VkImageView handler(+ VkSampler for now...)
class RenderTarget
{
public:
    using SharedPtr = std::shared_ptr<RenderTarget>;

    RenderTarget() = default;

    RenderTarget(VkDevice device,
                 VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                 core::memory::MemoryUsage memFlags = core::memory::MemoryUsage::GPU_ONLY, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL) : m_device(device), m_aspect(aspect), m_format(format)
    {
        m_image = core::Image::createShared(extent, usage, memFlags, format, tiling);
        m_extent = extent;
    }

    RenderTarget(VkDevice device, VkFormat format, VkImageAspectFlags aspect, core::Image::SharedPtr image) : m_device(device), m_format(format), m_aspect(aspect), m_image(image)
    {
        createVkImageView();
    }

    void resetVkImage(core::Image::SharedPtr image)
    {
        m_image = image;
    }

    void createVkImage(VkExtent2D extent, VkImageUsageFlags usage, core::memory::MemoryUsage memFlags, VkFormat format, VkImageTiling tiling)
    {
        m_extent = extent;
        m_image->createVk(extent, usage, memFlags, format, tiling);
    }

    void destroyVkImage()
    {
        m_image->destroyVk();
    }

    void createVkImageView()
    {
        VkImageViewCreateInfo imageViewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        imageViewCI.image = m_image->vk();
        imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCI.format = m_format;
        imageViewCI.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCI.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCI.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCI.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCI.subresourceRange.aspectMask = m_aspect;
        imageViewCI.subresourceRange.baseMipLevel = 0;
        imageViewCI.subresourceRange.levelCount = 1;
        imageViewCI.subresourceRange.baseArrayLayer = 0;
        imageViewCI.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device, &imageViewCI, nullptr, &m_imageView) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image views");
    }

    void destroyVkImageView()
    {
        if (m_imageView)
        {
            vkDestroyImageView(m_device, m_imageView, nullptr);
            m_imageView = VK_NULL_HANDLE;
        }
    }

    VkImageView vkImageView() const
    {
        return m_imageView;
    }

    core::Image::SharedPtr getImage() const
    {
        return m_image;
    }

    void setSampler(VkSampler sampler)
    {
        m_sampler = sampler;
    }

    VkSampler getSampler() const
    {
        return m_sampler;
    }

    ~RenderTarget()
    {
        destroyVkImage();
        destroyVkImageView();
    }

private:
    VkExtent2D m_extent;
    VkDevice m_device{VK_NULL_HANDLE};
    core::Image::SharedPtr m_image{nullptr};
    VkImageView m_imageView{VK_NULL_HANDLE};
    VkFormat m_format{VK_FORMAT_UNDEFINED};
    VkImageAspectFlags m_aspect;
    VkSampler m_sampler{VK_NULL_HANDLE}; // TODO it needs to be fixed
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_TARGET_HPP