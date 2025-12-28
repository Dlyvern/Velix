#include "Core/Memory/VMAAllocator.hpp"

#include <stdexcept>
#include "Core/VulkanHelpers.hpp"

#include <volk.h>
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

namespace
{
    //!Remove this function...
    VmaMemoryUsage deleteMeMemoryHelper(elix::core::memory::MemoryUsage usage)
    {
        switch (usage)
        {
            case elix::core::memory::MemoryUsage::GPU_ONLY: return VMA_MEMORY_USAGE_GPU_ONLY;
            case elix::core::memory::MemoryUsage::CPU_TO_GPU: return VMA_MEMORY_USAGE_CPU_TO_GPU;
            case elix::core::memory::MemoryUsage::GPU_TO_CPU: return VMA_MEMORY_USAGE_GPU_TO_CPU;
            case elix::core::memory::MemoryUsage::CPU_ONLY: return VMA_MEMORY_USAGE_CPU_ONLY;
            default: return VMA_MEMORY_USAGE_AUTO;
        }
    }
}

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(allocators)

VMAAllocator::VMAAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
const VkAllocationCallbacks* allocationCallbacks) : IAllocator(allocationCallbacks)
{
    VmaAllocatorCreateInfo info{};
    info.instance = instance;
    info.physicalDevice = physicalDevice;
    info.device = device;
    info.vulkanApiVersion = VK_API_VERSION_1_3;

    VmaVulkanFunctions volkFunctions{};

    vmaImportVulkanFunctionsFromVolk(&info, &volkFunctions);

    info.pVulkanFunctions = &volkFunctions;

    vmaCreateAllocator(&info, &m_allocator);
}

void VMAAllocator::mapMemory(VkDevice device, void* allocation, VkDeviceSize offset, VkDeviceSize size,  VkMemoryMapFlags flags, void*& data)
{
    if(VkResult result = vmaMapMemory(m_allocator, reinterpret_cast<VmaAllocation>(allocation), &data); result != VK_SUCCESS)   
        std::cerr << "Failed to map memory " << helpers::vulkanResultToString(result) << '\n';
}

void VMAAllocator::unmapMemory(VkDevice device, void* allocation)
{
    vmaUnmapMemory(m_allocator, reinterpret_cast<VmaAllocation>(allocation));
}

void VMAAllocator::allocateMemory(VkDevice device, const VkMemoryAllocateInfo& allocationInfo)
{
}

void VMAAllocator::freeMemory(VkDevice device, VkDeviceMemory memory)
{

}

AllocatedImage VMAAllocator::createImage(VkDevice device, VkPhysicalDevice physicalDevice, const VkImageCreateInfo& createInfo, memory::MemoryUsage memFlags)
{
    VmaMemoryUsage memoryUsage = static_cast<VmaMemoryUsage>(deleteMeMemoryHelper(memFlags));

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;

    AllocatedImage allocatedImage{};

    if(VkResult result = vmaCreateImage(m_allocator, &createInfo, &allocInfo, &allocatedImage.image,
    reinterpret_cast<VmaAllocation*>(&allocatedImage.allocation), nullptr); result != VK_SUCCESS)
        throw std::runtime_error("Failed to create image " + helpers::vulkanResultToString(result));

    return allocatedImage;
}

void VMAAllocator::destroyImage(VkDevice device, AllocatedImage& image)
{
    vmaDestroyImage(m_allocator, image.image, reinterpret_cast<VmaAllocation>(image.allocation));
    image.image = VK_NULL_HANDLE;
    image.allocation = nullptr;
}

void VMAAllocator::bindImageMemory(VkDevice device, const AllocatedImage& image, VkDeviceSize memoryOffset)
{
    if(VkResult result = vmaBindImageMemory(m_allocator, reinterpret_cast<VmaAllocation>(image.allocation), image.image); result != VK_SUCCESS)
        std::cerr << "Failed to bind image memory " << helpers::vulkanResultToString(result) << '\n';
}

AllocatedBuffer VMAAllocator::createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, const VkBufferCreateInfo& createInfo, memory::MemoryUsage memFlags)
{
    VmaMemoryUsage memoryUsage = static_cast<VmaMemoryUsage>(deleteMeMemoryHelper(memFlags));

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;

    AllocatedBuffer buffer{};

    if(VkResult result = vmaCreateBuffer(m_allocator, &createInfo, &allocInfo, &buffer.buffer, 
    reinterpret_cast<VmaAllocation*>(&buffer.allocation), nullptr); result != VK_SUCCESS)
        throw std::runtime_error("Failed to create buffer " + helpers::vulkanResultToString(result));

    return buffer;
}

void VMAAllocator::destroyBuffer(VkDevice device, AllocatedBuffer& buffer)
{
    vmaDestroyBuffer(m_allocator, buffer.buffer, reinterpret_cast<VmaAllocation>(buffer.allocation));
    buffer.buffer = VK_NULL_HANDLE;
    buffer.allocation = nullptr;
}

void VMAAllocator::bindBufferMemory(VkDevice device, const AllocatedBuffer& buffer, VkDeviceSize memoryOffset)
{
    vmaBindBufferMemory(m_allocator, reinterpret_cast<VmaAllocation>(buffer.allocation), buffer.buffer);
}

VkMemoryPropertyFlags VMAAllocator::toVkMemoryFlags(core::memory::MemoryUsage usage)
{
    switch (usage)
    {
        case memory::MemoryUsage::GPU_ONLY: return VMA_MEMORY_USAGE_GPU_ONLY;
        case memory::MemoryUsage::CPU_TO_GPU: return VMA_MEMORY_USAGE_CPU_TO_GPU;
        case memory::MemoryUsage::GPU_TO_CPU: return VMA_MEMORY_USAGE_GPU_TO_CPU;
        case memory::MemoryUsage::CPU_ONLY: return VMA_MEMORY_USAGE_CPU_ONLY;
        default: return VMA_MEMORY_USAGE_AUTO;
    }
}

VMAAllocator::~VMAAllocator()
{
    vmaDestroyAllocator(m_allocator);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END