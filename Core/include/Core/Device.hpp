#ifndef ELIX_DEVICE_HPP
#define ELIX_DEVICE_HPP

#include "Core/Macros.hpp"
#include "Core/Memory/IAllocator.hpp"

#include <cstdint>
#include <fstream>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class Device
{
    DECLARE_VK_HANDLE_METHODS(VkDevice)
    DECLARE_VK_SMART_PTRS(Device, VkDevice)
public:
    Device(VkDevice device, VkPhysicalDevice physicalDevice, std::unique_ptr<allocators::IAllocator> allocator = nullptr);

    void mapMemory(void *allocation, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void *&data);
    void unmapMemory(void *allocation);

    allocators::AllocatedImage createImage(const VkImageCreateInfo &createInfo, memory::MemoryUsage memFlags);
    void destroyImage(allocators::AllocatedImage &image);
    void bindImageMemory(const allocators::AllocatedImage &image, VkDeviceSize memoryOffset = 0);

    allocators::AllocatedBuffer createBuffer(const VkBufferCreateInfo &createInfo, memory::MemoryUsage memFlags);
    void destroyBuffer(allocators::AllocatedBuffer &buffer);
    void bindBufferMemory(const allocators::AllocatedBuffer &buffer, VkDeviceSize memoryOffset = 0);

    VkDeviceSize getTotalAllocatedVRAM() const;
    size_t getTotalUsedRAM() const;

    void clean();

private:
    std::unique_ptr<allocators::IAllocator> m_allocator{nullptr};
    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
#ifdef __linux__
    mutable bool m_isRAMFileOpened{false};
    mutable FILE *m_ramFile{nullptr};
#endif
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_DEVICE_HPP