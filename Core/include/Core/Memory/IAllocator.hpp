#ifndef ELIX_IALLOCATOR_HPP
#define ELIX_IALLOCATOR_HPP

#include "Core/Macros.hpp"
#include "Core/Memory/Allocators.hpp"
#include "Core/Memory/MemoryFlags.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(allocators)

class IAllocator
{
public:
    virtual VkMemoryPropertyFlags toVkMemoryFlags(core::memory::MemoryUsage usage) = 0;

    IAllocator(const VkAllocationCallbacks *allocationCallbacks = VK_NULL_HANDLE) : allocationCallbacks_(allocationCallbacks) {}

    virtual void mapMemory(VkDevice device, void *allocation, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void *&data) = 0;
    virtual void unmapMemory(VkDevice device, void *allocation) = 0;

    virtual void allocateMemory(VkDevice device, const VkMemoryAllocateInfo &allocationInfo) = 0;
    virtual void freeMemory(VkDevice device, VkDeviceMemory memory) = 0;

    virtual AllocatedImage createImage(VkDevice device, VkPhysicalDevice physicalDevice, const VkImageCreateInfo &createInfo, memory::MemoryUsage memFlags) = 0;
    virtual void destroyImage(VkDevice device, AllocatedImage &image) = 0;
    virtual void bindImageMemory(VkDevice device, const AllocatedImage &image, VkDeviceSize memoryOffset = 0) = 0;

    virtual AllocatedBuffer createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, const VkBufferCreateInfo &createInfo, memory::MemoryUsage memFlags) = 0;
    virtual void destroyBuffer(VkDevice device, AllocatedBuffer &buffer) = 0;
    virtual void bindBufferMemory(VkDevice device, const AllocatedBuffer &buffer, VkDeviceSize memoryOffset = 0) = 0;

    virtual void clean() {}

    virtual VkDeviceSize getTotalAllocatedVRAM() const = 0;

    virtual ~IAllocator() = default;

protected:
    const VkAllocationCallbacks *allocationCallbacks_;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_IALLOCATOR_HPP