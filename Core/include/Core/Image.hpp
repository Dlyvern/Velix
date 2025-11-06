#ifndef ELIX_IMAGE_HPP
#define ELIX_IMAGE_HPP

#include "Core/Macros.hpp"
#include "Core/CommandPool.hpp"
#include "Core/Buffer.hpp"
#include "Core/VulkanHelpers.hpp"

#include <volk.h>

#include <cstdint>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class Image
{
public:
    using SharedPtr = std::shared_ptr<Image>;
    using UniquePtr = std::unique_ptr<Image>;
    using WeakPtr = std::weak_ptr<Image>;

    Image(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB, 
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL, uint32_t arrayLayers = 1, VkImageCreateFlags flags = 0) : m_device(device)
    {
        createVk(physicalDevice, VkExtent2D{.width = width, .height = height}, usage, properties, format, tiling, arrayLayers, flags);
    }

    Image(VkDevice device, VkImage image) : m_device(device)
    {
        m_image = image;
    }

    /*
        TODO: Document this
    */
    void createVk(VkPhysicalDevice physicalDevice, VkExtent2D extent, VkImageUsageFlags usage, 
    VkMemoryPropertyFlags properties, VkFormat format, VkImageTiling tiling, uint32_t arrayLayers = 1, VkImageCreateFlags flags = 0)
    {
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

        if(vkCreateImage(m_device, &imageInfo, nullptr, &m_image) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image");
        
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(m_device, m_image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = helpers::findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);
        
        if(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_imageMemory) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate image memory");
        
        vkBindImageMemory(m_device, m_image, m_imageMemory, 0);
    }

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    void insertImageMemoryBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
    VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
    VkImageSubresourceRange subresourceRange, CommandPool::SharedPtr commandPool, VkQueue queue)
    {
        auto cb = CommandBuffer::create(m_device, commandPool->vk());
        cb->begin();

        VkImageMemoryBarrier imageMemoryBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageMemoryBarrier.srcAccessMask = srcAccessMask;
        imageMemoryBarrier.dstAccessMask = dstAccessMask;
        imageMemoryBarrier.oldLayout = oldImageLayout;
        imageMemoryBarrier.newLayout = newImageLayout;
        imageMemoryBarrier.image = m_image;
        imageMemoryBarrier.subresourceRange = subresourceRange;

        vkCmdPipelineBarrier(cb->vk(), srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

        cb->end();

        cb->submit(queue, {}, {}, {}, VK_NULL_HANDLE);

        vkQueueWaitIdle(queue);
    }

    void transitionImageLayout(VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, CommandPool::SharedPtr commandPool, VkQueue queue, uint32_t layerCount = 1)
    {
        auto cb = CommandBuffer::create(m_device, commandPool->vk());
        cb->begin();

        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = layerCount;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if(newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

            if(helpers::hasStencilComponent(format))
                barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        else 
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        if(oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if(oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if(oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        else
            throw std::runtime_error("Unsuported layout transition");

        vkCmdPipelineBarrier(cb->vk(), sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        cb->end();

        cb->submit(queue, {}, {}, {}, VK_NULL_HANDLE);

        vkQueueWaitIdle(queue);
    }

    void copyBufferToImage(Buffer::SharedPtr buffer, uint32_t width, uint32_t height, CommandPool::SharedPtr commandPool, VkQueue queue, uint32_t layerCount = 1)
    {
        auto cb = CommandBuffer::create(m_device, commandPool->vk());
        cb->begin();

        VkDeviceSize offset = 0;
        std::vector<VkBufferImageCopy> regions;
        for (uint32_t layer = 0; layer < layerCount; ++layer)
        {
            VkBufferImageCopy region{};
            region.bufferOffset = offset;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = layer;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {width, height, 1};
            
            regions.push_back(region);
            offset += width * height * 4;
        }
        
        vkCmdCopyBufferToImage(cb->vk(), buffer->vkBuffer(), m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()), regions.data());
        
        cb->end();

        cb->submit(queue, {}, {}, {}, VK_NULL_HANDLE);

        vkQueueWaitIdle(queue);
    }

    VkImage vk()
    {
        return m_image;
    }

    static SharedPtr create(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB, 
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL, uint32_t arrayLayers = 1, VkImageCreateFlags flags = 0)
    {
        return std::make_shared<Image>(device, physicalDevice, width, height, usage, properties, format, tiling, arrayLayers, flags);
    }

    static SharedPtr wrap(VkDevice device, VkImage image)
    {
        auto wrappedImage = std::make_shared<Image>(device, image);
        wrappedImage->m_isWrapped = true;
        return wrappedImage;
    }

    void destroyVk()
    {
        if(!m_isWrapped)
        {
            if (m_imageMemory)
            {
                vkFreeMemory(m_device, m_imageMemory, nullptr);
                m_imageMemory = VK_NULL_HANDLE;
            }
            if (m_image)
            {
                vkDestroyImage(m_device, m_image, nullptr);
                m_image = VK_NULL_HANDLE;
            }
        }
    }

    ~Image()
    {
        destroyVk();
    }
private:
    VkImage m_image{VK_NULL_HANDLE};
    VkDeviceMemory m_imageMemory{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    bool m_isWrapped{false};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_IMAGE_HPP