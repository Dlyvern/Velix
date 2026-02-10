#ifndef ELIX_IMAGE_HPP
#define ELIX_IMAGE_HPP

#include "Core/Macros.hpp"
#include "Core/CommandPool.hpp"
#include "Core/Buffer.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Core/Memory/Allocators.hpp"
#include "Core/Memory/MemoryFlags.hpp"
#include <volk.h>

#include <cstdint>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class Image
{
    DECLARE_VK_HANDLE_METHODS(VkImage)
    DECLARE_VK_SMART_PTRS(Image, VkImage)
    ELIX_DECLARE_VK_LIFECYCLE()
public:
    Image(uint32_t width, uint32_t height, VkImageUsageFlags usage, memory::MemoryUsage memFlags, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
          VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL, uint32_t arrayLayers = 1, VkImageCreateFlags flags = 0);

    Image(VkImage image);

    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;

    void bind(VkDeviceSize memoryOffset = 0);

    void createVk(VkExtent2D extent, VkImageUsageFlags usage,
                  memory::MemoryUsage memFlags, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL, uint32_t arrayLayers = 1, VkImageCreateFlags flags = 0);

    void insertImageMemoryBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
                                  VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                                  VkImageSubresourceRange subresourceRange, CommandPool::SharedPtr commandPool = nullptr, VkQueue queue = VK_NULL_HANDLE);

    void insertImageMemoryBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
                                  VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                                  VkImageSubresourceRange subresourceRange, CommandBuffer &commandBuffer);

    void transitionImageLayout(VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, CommandPool::SharedPtr commandPool = nullptr, VkQueue queue = VK_NULL_HANDLE, uint32_t layerCount = 1);

    void copyBufferToImage(Buffer::SharedPtr buffer, uint32_t width, uint32_t height, CommandPool::SharedPtr commandPool, VkQueue queue, uint32_t layerCount = 1, uint32_t baseLayer = 0);

    ~Image();

private:
    allocators::AllocatedImage m_allocatedImage;
    VkDevice m_device{VK_NULL_HANDLE};
    bool m_isWrapped{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_IMAGE_HPP