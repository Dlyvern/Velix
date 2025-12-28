#ifndef ELIX_VMA_ALLOCATOR_HPP
#define ELIX_VMA_ALLOCATOR_HPP

#include "Core/Memory/IAllocator.hpp"

#include "vk_mem_alloc.h"

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(allocators)

class VMAAllocator final : public IAllocator
{
public:
    VMAAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
    const VkAllocationCallbacks* allocationCallbacks = VK_NULL_HANDLE);

    void mapMemory(VkDevice device, void* allocation, VkDeviceSize offset, VkDeviceSize size,  VkMemoryMapFlags flags, void*& data) override;
    void unmapMemory(VkDevice device, void* allocation) override;

    void allocateMemory(VkDevice device, const VkMemoryAllocateInfo& allocationInfo) override;
    void freeMemory(VkDevice device, VkDeviceMemory memory) override;

    AllocatedImage createImage(VkDevice device, VkPhysicalDevice physicalDevice, const VkImageCreateInfo& createInfo, memory::MemoryUsage memFlags) override;
    void destroyImage(VkDevice device, AllocatedImage& image) override;
    void bindImageMemory(VkDevice device, const AllocatedImage& image, VkDeviceSize memoryOffset = 0) override;

    AllocatedBuffer createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, const VkBufferCreateInfo& createInfo, memory::MemoryUsage memFlags) override;
    void destroyBuffer(VkDevice device, AllocatedBuffer& buffer) override;
    void bindBufferMemory(VkDevice device, const AllocatedBuffer& buffer, VkDeviceSize memoryOffset = 0) override;

    VkMemoryPropertyFlags toVkMemoryFlags(core::memory::MemoryUsage usage) override;

    ~VMAAllocator() override;
private:
    VmaAllocator m_allocator{nullptr};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif //ELIX_VMA_ALLOCATOR_HPP