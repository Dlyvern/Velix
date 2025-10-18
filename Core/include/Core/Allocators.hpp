#ifndef ELIX_ALLOCATORS_HPP
#define ELIX_ALLOCATORS_HPP

#include "Core/Macros.hpp"

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(allocators)

class IAllocator
{
public:
    IAllocator(const VkAllocationCallbacks* allocationCallbacks = VK_NULL_HANDLE) : allocationCallbacks_(allocationCallbacks) {}

    virtual void allocateMemory(VkDevice device, const VkMemoryAllocateInfo& allocationInfo) = 0;
    virtual void freeMemory(VkDevice device, VkDeviceMemory memory) = 0;

    virtual void createImage(VkDevice device, const VkImageCreateInfo& createInfo) = 0;
    virtual void destroyImage(VkDevice device, VkImage image) = 0;

    virtual void createBuffer(VkDevice device, const VkBufferCreateInfo& createInfo) = 0;
    virtual void destroyBuffer(VkDevice device, VkBuffer buffer) = 0;

    virtual ~IAllocator() = default;
protected:
    const VkAllocationCallbacks* allocationCallbacks_;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif //ELIX_ALLOCATORS_HPP