#include "Core/Memory/DefaultAllocator.hpp"
#include "Core/VulkanHelpers.hpp"

#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(allocators)

DefaultAllocator::DefaultAllocator(const VkAllocationCallbacks* allocationCallbacks) : IAllocator(allocationCallbacks)
{

}

void DefaultAllocator::allocateMemory(VkDevice device, const VkMemoryAllocateInfo& allocationInfo)
{

}

void DefaultAllocator::freeMemory(VkDevice device, VkDeviceMemory memory)
{

}

AllocatedImage DefaultAllocator::createImage(VkDevice device, VkPhysicalDevice physicalDevice, const VkImageCreateInfo& createInfo, memory::MemoryUsage memFlags)
{
    AllocatedImage allocatedImage;

    if(vkCreateImage(device, &createInfo, allocationCallbacks_, &allocatedImage.image) != VK_SUCCESS)
        throw std::runtime_error("Failed to create image");
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, allocatedImage.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = helpers::findMemoryType(physicalDevice, memRequirements.memoryTypeBits, toVkMemoryFlags(memFlags));
    
    if(vkAllocateMemory(device, &allocInfo, allocationCallbacks_, reinterpret_cast<VkDeviceMemory*>(&allocatedImage.allocation)) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate image memory");
    
    bindImageMemory(device, allocatedImage);

    return allocatedImage;
}

void DefaultAllocator::destroyImage(VkDevice device, AllocatedImage& image)
{
    if(image.allocation)
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

void DefaultAllocator::bindImageMemory(VkDevice device, const AllocatedImage& image, VkDeviceSize memoryOffset)
{
    vkBindImageMemory(device, image.image, reinterpret_cast<VkDeviceMemory>(image.allocation), memoryOffset);
}

void DefaultAllocator::mapMemory(VkDevice device, void* allocation, VkDeviceSize offset, VkDeviceSize size,  VkMemoryMapFlags flags, void*& data)
{
    vkMapMemory(device, reinterpret_cast<VkDeviceMemory>(allocation), offset, size, flags, &data);
}

void DefaultAllocator::unmapMemory(VkDevice device, void* allocation)
{
    vkUnmapMemory(device, reinterpret_cast<VkDeviceMemory>(allocation));
}

AllocatedBuffer DefaultAllocator::createBuffer(VkDevice device,  VkPhysicalDevice physicalDevice, const VkBufferCreateInfo& createInfo, memory::MemoryUsage memFlags)
{
    AllocatedBuffer buffer;

    if(VkResult result = vkCreateBuffer(device, &createInfo, allocationCallbacks_, &buffer.buffer); result != VK_SUCCESS)
        throw std::runtime_error("Failed to create buffer");

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer.buffer, &memRequirements);

    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = memRequirements.size;
    allocateInfo.memoryTypeIndex = core::helpers::findMemoryType(physicalDevice, memRequirements.memoryTypeBits, toVkMemoryFlags(memFlags));

    if(vkAllocateMemory(device, &allocateInfo, allocationCallbacks_, reinterpret_cast<VkDeviceMemory*>(&buffer.allocation)) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate buffer memory");

    bindBufferMemory(device, buffer);

    return buffer;
}

void DefaultAllocator::destroyBuffer(VkDevice device, AllocatedBuffer& buffer)
{
    if(buffer.buffer)
    {
        vkDestroyBuffer(device, buffer.buffer, allocationCallbacks_);
        buffer.buffer = VK_NULL_HANDLE;
    }
    
    if(buffer.allocation)
    {
        vkFreeMemory(device, reinterpret_cast<VkDeviceMemory>(buffer.allocation), allocationCallbacks_);
        buffer.allocation = nullptr;
    }
}

void DefaultAllocator::bindBufferMemory(VkDevice device, const AllocatedBuffer& buffer, VkDeviceSize memoryOffset)
{
    vkBindBufferMemory(device, buffer.buffer, reinterpret_cast<VkDeviceMemory>(buffer.allocation), memoryOffset);
}

VkMemoryPropertyFlags DefaultAllocator::toVkMemoryFlags(core::memory::MemoryUsage usage)
{
    switch (usage)
    {
        case memory::MemoryUsage::GPU_ONLY: return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        case memory::MemoryUsage::CPU_TO_GPU: return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        case memory::MemoryUsage::GPU_TO_CPU: return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        case memory::MemoryUsage::CPU_ONLY: return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        default: return 0;
    }
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END