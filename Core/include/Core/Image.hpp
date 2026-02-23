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
    Image(VkExtent2D extent, VkImageUsageFlags usage, memory::MemoryUsage memFlags, VkFormat format,
          VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL, uint32_t arrayLayers = 1, VkImageCreateFlags flags = 0);

    Image(VkImage image);

    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;

    void bind(VkDeviceSize memoryOffset = 0);

    void createVk(VkExtent2D extent, VkImageUsageFlags usage,
                  memory::MemoryUsage memFlags, VkFormat format, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL, uint32_t arrayLayers = 1, VkImageCreateFlags flags = 0);

    ~Image();

private:
    allocators::AllocatedImage m_allocatedImage;
    VkDevice m_device{VK_NULL_HANDLE};
    bool m_isWrapped{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_IMAGE_HPP