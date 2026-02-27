#include "Core/Memory/VMAAllocator.hpp"

#include "Core/VulkanHelpers.hpp"
#include "Core/VulkanAssert.hpp"
#include <iomanip>
#include <sstream>
#include <volk.h>
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

namespace
{
    uint32_t clampVulkanApiVersionForVma(uint32_t apiVersion)
    {
#if VMA_VULKAN_VERSION >= 1004000
        constexpr uint32_t maxSupportedApiVersion = VK_MAKE_VERSION(1, 4, 0);
#elif VMA_VULKAN_VERSION >= 1003000
        constexpr uint32_t maxSupportedApiVersion = VK_MAKE_VERSION(1, 3, 0);
#elif VMA_VULKAN_VERSION >= 1002000
        constexpr uint32_t maxSupportedApiVersion = VK_MAKE_VERSION(1, 2, 0);
#elif VMA_VULKAN_VERSION >= 1001000
        constexpr uint32_t maxSupportedApiVersion = VK_MAKE_VERSION(1, 1, 0);
#else
        constexpr uint32_t maxSupportedApiVersion = VK_MAKE_VERSION(1, 0, 0);
#endif

        return std::min(apiVersion, maxSupportedApiVersion);
    }

    // TODO Remove this function...
    VmaMemoryUsage deleteMeMemoryHelper(elix::core::memory::MemoryUsage usage)
    {
        switch (usage)
        {
        case elix::core::memory::MemoryUsage::GPU_ONLY:
            return VMA_MEMORY_USAGE_GPU_ONLY;
        case elix::core::memory::MemoryUsage::CPU_TO_GPU:
            return VMA_MEMORY_USAGE_CPU_TO_GPU;
        case elix::core::memory::MemoryUsage::GPU_TO_CPU:
            return VMA_MEMORY_USAGE_GPU_TO_CPU;
        case elix::core::memory::MemoryUsage::CPU_ONLY:
            return VMA_MEMORY_USAGE_CPU_ONLY;
        case elix::core::memory::MemoryUsage::AUTO:
            return VMA_MEMORY_USAGE_AUTO;
        default:
            return VMA_MEMORY_USAGE_AUTO;
        }
    }

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
}

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(allocators)

VMAAllocator::VMAAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                           uint32_t vulkanApiVersion,
                           const VkAllocationCallbacks *allocationCallbacks) : IAllocator(allocationCallbacks)
{
    VmaAllocatorCreateInfo info{};
    info.instance = instance;
    info.physicalDevice = physicalDevice;
    info.device = device;
    info.vulkanApiVersion = clampVulkanApiVersionForVma(vulkanApiVersion);

    VmaVulkanFunctions volkFunctions{};

    VX_VK_CHECK(vmaImportVulkanFunctionsFromVolk(&info, &volkFunctions));

    info.pVulkanFunctions = &volkFunctions;

    VX_VK_CHECK(vmaCreateAllocator(&info, &m_allocator));
}

void VMAAllocator::mapMemory(VkDevice device, void *allocation, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void *&data)
{
    VX_VK_CHECK(vmaMapMemory(m_allocator, reinterpret_cast<VmaAllocation>(allocation), &data));
}

void VMAAllocator::unmapMemory(VkDevice device, void *allocation)
{
    vmaUnmapMemory(m_allocator, reinterpret_cast<VmaAllocation>(allocation));
}

void VMAAllocator::allocateMemory(VkDevice device, const VkMemoryAllocateInfo &allocationInfo)
{
}

void VMAAllocator::freeMemory(VkDevice device, VkDeviceMemory memory)
{
}

AllocatedImage VMAAllocator::createImage(VkDevice device, VkPhysicalDevice physicalDevice, const VkImageCreateInfo &createInfo, memory::MemoryUsage memFlags)
{
    VmaMemoryUsage memoryUsage = static_cast<VmaMemoryUsage>(deleteMeMemoryHelper(memFlags));

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;

    AllocatedImage allocatedImage{};

    VX_VK_CHECK_MSG(vmaCreateImage(m_allocator, &createInfo, &allocInfo, &allocatedImage.image,
                                   reinterpret_cast<VmaAllocation *>(&allocatedImage.allocation), nullptr),
                    describeImageCreateInfo(createInfo));

    return allocatedImage;
}

void VMAAllocator::destroyImage(VkDevice device, AllocatedImage &image)
{
    vmaDestroyImage(m_allocator, image.image, reinterpret_cast<VmaAllocation>(image.allocation));
    image.image = VK_NULL_HANDLE;
    image.allocation = nullptr;
}

void VMAAllocator::bindImageMemory(VkDevice device, const AllocatedImage &image, VkDeviceSize memoryOffset)
{
    VX_VK_CHECK(vmaBindImageMemory(m_allocator, reinterpret_cast<VmaAllocation>(image.allocation), image.image));
}

AllocatedBuffer VMAAllocator::createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, const VkBufferCreateInfo &createInfo, memory::MemoryUsage memFlags)
{
    VmaMemoryUsage memoryUsage = static_cast<VmaMemoryUsage>(deleteMeMemoryHelper(memFlags));

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;

    AllocatedBuffer buffer{};

    VX_VK_CHECK_MSG(vmaCreateBuffer(m_allocator, &createInfo, &allocInfo, &buffer.buffer,
                                    reinterpret_cast<VmaAllocation *>(&buffer.allocation), nullptr),
                    describeBufferCreateInfo(createInfo));

    return buffer;
}

VkDeviceSize VMAAllocator::getTotalAllocatedVRAM() const
{
    VmaTotalStatistics stats{};
    vmaCalculateStatistics(m_allocator, &stats);

    auto bytes = stats.total.statistics.allocationBytes;

    double megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);

    return megabytes;
    // printf("Total allocation count: %u\n",
    //        stats.total.statistics.allocationCount);
}

void VMAAllocator::destroyBuffer(VkDevice device, AllocatedBuffer &buffer)
{
    vmaDestroyBuffer(m_allocator, buffer.buffer, reinterpret_cast<VmaAllocation>(buffer.allocation));
    buffer.buffer = VK_NULL_HANDLE;
    buffer.allocation = nullptr;
}

void VMAAllocator::bindBufferMemory(VkDevice device, const AllocatedBuffer &buffer, VkDeviceSize memoryOffset)
{
    VX_VK_CHECK(vmaBindBufferMemory(m_allocator, reinterpret_cast<VmaAllocation>(buffer.allocation), buffer.buffer));
}

VkMemoryPropertyFlags VMAAllocator::toVkMemoryFlags(core::memory::MemoryUsage usage)
{
    switch (usage)
    {
    case memory::MemoryUsage::GPU_ONLY:
        return VMA_MEMORY_USAGE_GPU_ONLY;
    case memory::MemoryUsage::CPU_TO_GPU:
        return VMA_MEMORY_USAGE_CPU_TO_GPU;
    case memory::MemoryUsage::GPU_TO_CPU:
        return VMA_MEMORY_USAGE_GPU_TO_CPU;
    case memory::MemoryUsage::CPU_ONLY:
        return VMA_MEMORY_USAGE_CPU_ONLY;
    case memory::MemoryUsage::AUTO:
        return VMA_MEMORY_USAGE_AUTO;
    default:
        return VMA_MEMORY_USAGE_AUTO;
    }
}

void VMAAllocator::clean()
{
    if (m_allocator)
    {
        vmaDestroyAllocator(m_allocator);
        m_allocator = nullptr;
    }
}

VMAAllocator::~VMAAllocator()
{
    clean();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
