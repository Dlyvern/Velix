#ifndef ELIX_IMAGE_HPP
#define ELIX_IMAGE_HPP

#include "Core/Macros.hpp"
#include "Core/CommandPool.hpp"
#include "Core/Buffer.hpp"

#include <volk.h>

#include <cstdint>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class Image
{
public:
    using SharedPtr = std::shared_ptr<Image>;

    Image(VkDevice device, uint32_t width, uint32_t height, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB, 
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL);

    Image(VkDevice device, VkImage image);

    void insertImageMemoryBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
    VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
    VkImageSubresourceRange subresourceRange, CommandPool::SharedPtr commandPool, VkQueue queue);

    void transitionImageLayout(VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, CommandPool::SharedPtr commandPool, VkQueue queue);

    void copyBufferToImage(Buffer::SharedPtr buffer, uint32_t width, uint32_t height, CommandPool::SharedPtr commandPool, VkQueue queue);

    VkImage vk();

    static SharedPtr create(VkDevice device, uint32_t width, uint32_t height, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB, 
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL);

    void destroy();

    ~Image();
private:
    bool m_isDestroyed{false};
    VkImage m_image{VK_NULL_HANDLE};
    VkDeviceMemory m_imageMemory{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_IMAGE_HPP