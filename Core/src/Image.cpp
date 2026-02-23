#include "Core/Image.hpp"
#include "Core/VulkanContext.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)

Image::Image(VkExtent2D extent, VkImageUsageFlags usage, memory::MemoryUsage memFlags, VkFormat format,
             VkImageTiling tiling, uint32_t arrayLayers, VkImageCreateFlags flags) : m_device(VulkanContext::getContext()->getDevice())
{
    createVk(extent, usage, memFlags, format, tiling, arrayLayers, flags);
}

void Image::createVk(VkExtent2D extent, VkImageUsageFlags usage, memory::MemoryUsage memFlags, VkFormat format,
                     VkImageTiling tiling, uint32_t arrayLayers, VkImageCreateFlags flags)
{
    ELIX_VK_CREATE_GUARD()

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = extent.width;
    imageInfo.extent.height = extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = flags;

    m_allocatedImage = VulkanContext::getContext()->getDevice()->createImage(imageInfo, memFlags);
    m_handle = m_allocatedImage.image;

    ELIX_VK_CREATE_GUARD_DONE()
}

void Image::bind(VkDeviceSize memoryOffset)
{
    VulkanContext::getContext()->getDevice()->bindImageMemory(m_allocatedImage, memoryOffset);
}

Image::Image(VkImage image) : m_device(VulkanContext::getContext()->getDevice())
{
    m_handle = image;
    m_allocatedImage.image = image;
    m_isWrapped = true;
}

void Image::destroyVkImpl()
{
    if (!m_isWrapped)
    {
        VulkanContext::getContext()->getDevice()->destroyImage(m_allocatedImage);
        m_handle = m_allocatedImage.image;
    }
}

Image::~Image()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END