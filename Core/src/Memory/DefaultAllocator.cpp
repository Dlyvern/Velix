#include "Core/Memory/DefaultAllocator.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Core/VulkanAssert.hpp"

#include <sstream>

namespace
{
std::string describeImageCreateInfo(const VkImageCreateInfo &createInfo)
{
    std::ostringstream stream;
    stream << "format=" << static_cast<int>(createInfo.format)
           << ", extent=" << createInfo.extent.width << "x" << createInfo.extent.height << "x" << createInfo.extent.depth
           << ", usage=0x" << std::hex << createInfo.usage << std::dec
           << ", tiling=" << static_cast<int>(createInfo.tiling)
           << ", arrayLayers=" << createInfo.arrayLayers
           << ", mipLevels=" << createInfo.mipLevels
           << ", samples=" << static_cast<int>(createInfo.samples)
           << ", flags=0x" << std::hex << createInfo.flags << std::dec;
    return stream.str();
}

std::string describeBufferCreateInfo(const VkBufferCreateInfo &createInfo)
{
    std::ostringstream stream;
    stream << "size=" << static_cast<unsigned long long>(createInfo.size)
           << ", usage=0x" << std::hex << createInfo.usage << std::dec
           << ", sharingMode=" << static_cast<int>(createInfo.sharingMode)
           << ", flags=0x" << std::hex << createInfo.flags << std::dec;
    return stream.str();
}
} // namespace

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(allocators)

DefaultAllocator::DefaultAllocator(const VkAllocationCallbacks *allocationCallbacks) : IAllocator(allocationCallbacks)
{
}

void DefaultAllocator::allocateMemory(VkDevice device, const VkMemoryAllocateInfo &allocationInfo)
{
}

void DefaultAllocator::freeMemory(VkDevice device, VkDeviceMemory memory)
{
}

VkDeviceSize DefaultAllocator::getTotalAllocatedVRAM() const
{
    return VkDeviceSize(0);
}

AllocatedImage DefaultAllocator::createImage(VkDevice device, VkPhysicalDevice physicalDevice, const VkImageCreateInfo &createInfo, memory::MemoryUsage memFlags)
{
    AllocatedImage allocatedImage;

    VX_VK_CHECK_MSG(vkCreateImage(device, &createInfo, allocationCallbacks_, &allocatedImage.image), describeImageCreateInfo(createInfo));

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, allocatedImage.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = helpers::findMemoryType(physicalDevice, memRequirements.memoryTypeBits, toVkMemoryFlags(memFlags));

    VX_VK_CHECK(vkAllocateMemory(device, &allocInfo, allocationCallbacks_, reinterpret_cast<VkDeviceMemory *>(&allocatedImage.allocation)));

    bindImageMemory(device, allocatedImage);

    return allocatedImage;
}

void DefaultAllocator::destroyImage(VkDevice device, AllocatedImage &image)
{
    if (image.allocation)
    {
        vkFreeMemory(device, reinterpret_cast<VkDeviceMemory>(image.allocation), allocationCallbacks_);
        image.allocation = VK_NULL_HANDLE;
    }

    if (image.image)
    {
        vkDestroyImage(device, image.image, allocationCallbacks_);
        image.image = VK_NULL_HANDLE;
    }
}

void DefaultAllocator::bindImageMemory(VkDevice device, const AllocatedImage &image, VkDeviceSize memoryOffset)
{
    VX_VK_CHECK(vkBindImageMemory(device, image.image, reinterpret_cast<VkDeviceMemory>(image.allocation), memoryOffset));
}

void DefaultAllocator::mapMemory(VkDevice device, void *allocation, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void *&data)
{
    VX_VK_CHECK(vkMapMemory(device, reinterpret_cast<VkDeviceMemory>(allocation), offset, size, flags, &data));
}

void DefaultAllocator::unmapMemory(VkDevice device, void *allocation)
{
    vkUnmapMemory(device, reinterpret_cast<VkDeviceMemory>(allocation));
}

AllocatedBuffer DefaultAllocator::createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, const VkBufferCreateInfo &createInfo, memory::MemoryUsage memFlags)
{
    AllocatedBuffer buffer;

    VX_VK_CHECK_MSG(vkCreateBuffer(device, &createInfo, allocationCallbacks_, &buffer.buffer), describeBufferCreateInfo(createInfo));

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer.buffer, &memRequirements);

    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = memRequirements.size;
    allocateInfo.memoryTypeIndex = core::helpers::findMemoryType(physicalDevice, memRequirements.memoryTypeBits, toVkMemoryFlags(memFlags));

    VX_VK_CHECK(vkAllocateMemory(device, &allocateInfo, allocationCallbacks_, reinterpret_cast<VkDeviceMemory *>(&buffer.allocation)));

    bindBufferMemory(device, buffer);

    return buffer;
}

void DefaultAllocator::destroyBuffer(VkDevice device, AllocatedBuffer &buffer)
{
    if (buffer.buffer)
    {
        vkDestroyBuffer(device, buffer.buffer, allocationCallbacks_);
        buffer.buffer = VK_NULL_HANDLE;
    }

    if (buffer.allocation)
    {
        vkFreeMemory(device, reinterpret_cast<VkDeviceMemory>(buffer.allocation), allocationCallbacks_);
        buffer.allocation = nullptr;
    }
}

void DefaultAllocator::bindBufferMemory(VkDevice device, const AllocatedBuffer &buffer, VkDeviceSize memoryOffset)
{
    VX_VK_CHECK(vkBindBufferMemory(device, buffer.buffer, reinterpret_cast<VkDeviceMemory>(buffer.allocation), memoryOffset));
}

VkMemoryPropertyFlags DefaultAllocator::toVkMemoryFlags(core::memory::MemoryUsage usage)
{
    switch (usage)
    {
    case memory::MemoryUsage::GPU_ONLY:
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    case memory::MemoryUsage::CPU_TO_GPU:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    case memory::MemoryUsage::GPU_TO_CPU:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    case memory::MemoryUsage::CPU_ONLY:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    default:
        return 0;
    }
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
