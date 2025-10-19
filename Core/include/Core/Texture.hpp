#ifndef ELIX_TEXTURE_HPP
#define ELIX_TEXTURE_HPP

#include "Core/Macros.hpp"
#include "Core/Image.hpp"

#include <volk.h>

#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(core)

//VkImage + VkImageView handler
template<typename Deleter = ImageDeleter>
class Texture
{
public:    
    using SharedPtr = std::shared_ptr<Texture<Deleter>>;
    
    Texture(VkDevice device, VkPhysicalDevice physicalDevice,
            VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
            VkMemoryPropertyFlags memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL, uint32_t mipLevels = 0, uint32_t arrayLayers = 1,
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) : m_device(device), m_aspect(aspect), m_format(format), m_physicalDevice(physicalDevice)
    {
        m_image = Image<Deleter>::createCustom(device, physicalDevice, extent.width, extent.height, usage, memoryProperties, format, tiling);
    }

    Texture(VkDevice device, VkPhysicalDevice physicalDevice, VkFormat format, VkImageAspectFlags aspect, core::Image<Deleter>::SharedPtr image) :
    m_device(device), m_physicalDevice(physicalDevice), m_format(format), m_aspect(aspect), m_image(image)
    {
        createVkImageView();
    }

    void resetVkImage(core::Image<core::ImageNoDelete>::SharedPtr image)
    {
        m_image = image;
    }

    void createVkImage(VkExtent2D extent, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkFormat format, VkImageTiling tiling)
    {
        m_image->createVk(m_physicalDevice, extent, usage, properties, format, tiling);
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

        if(vkCreateImageView(m_device, &imageViewCI, nullptr, &m_imageView) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image views");
    }

    void destroyVkImageView()
    {
        if(m_imageView)
        {
            vkDestroyImageView(m_device, m_imageView, nullptr);
            m_imageView = VK_NULL_HANDLE;
        }
    }

    VkImageView vkImageView() const
    {
        return m_imageView;
    }

    core::Image<Deleter>::SharedPtr getImage() const
    {
        return m_image;
    }

    ~Texture()
    {
        destroyVkImage();
        destroyVkImageView();
    }
private:
    VkDevice m_device{VK_NULL_HANDLE};
    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    core::Image<Deleter>::SharedPtr m_image{nullptr};
    VkImageView m_imageView{VK_NULL_HANDLE};
    VkFormat m_format{VK_FORMAT_UNDEFINED};
    VkImageAspectFlags m_aspect;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_TEXTURE_HPP