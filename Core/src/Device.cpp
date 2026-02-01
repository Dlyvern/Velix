#include "Core/Device.hpp"
#include "Core/Memory/DefaultAllocator.hpp"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <string>
#include <unistd.h>
#endif

ELIX_NESTED_NAMESPACE_BEGIN(core)

Device::Device(VkDevice device, VkPhysicalDevice physicalDevice, std::unique_ptr<allocators::IAllocator> allocator)
    : m_handle(device), m_physicalDevice(physicalDevice)
{
    if (!allocator)
        allocator = std::make_unique<allocators::DefaultAllocator>();

    m_allocator = std::move(allocator);
}

size_t Device::getTotalUsedRAM() const
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS info;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info)))
        return (size_t)info.WorkingSetSize;
    return 0L;
#else
    long rss = 0L;

    if (!m_isRAMFileOpened)
    {
        if ((m_ramFile = fopen("/proc/self/statm", "r")) == NULL)
            return 0L;

        m_isRAMFileOpened = true;
    }

    fseek(m_ramFile, 0, SEEK_SET);

    if (fscanf(m_ramFile, "%*s%ld", &rss) != 1)
    {
        m_isRAMFileOpened = false;
        fclose(m_ramFile);
        return 0L;
    }

    auto bytes = (size_t)rss * (size_t)sysconf(_SC_PAGESIZE);
    return (size_t)(bytes / (1024 * 1024));
#endif
}

void Device::mapMemory(void *allocation, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void *&data)
{
    m_allocator->mapMemory(m_handle, allocation, offset, size, flags, data);
}

void Device::unmapMemory(void *allocation)
{
    m_allocator->unmapMemory(m_handle, allocation);
}

allocators::AllocatedImage Device::createImage(const VkImageCreateInfo &createInfo, memory::MemoryUsage memFlags)
{
    return m_allocator->createImage(m_handle, m_physicalDevice, createInfo, memFlags);
}

void Device::destroyImage(allocators::AllocatedImage &image)
{
    m_allocator->destroyImage(m_handle, image);
}

void Device::bindImageMemory(const allocators::AllocatedImage &image, VkDeviceSize memoryOffset)
{
    m_allocator->bindImageMemory(m_handle, image, memoryOffset);
}

allocators::AllocatedBuffer Device::createBuffer(const VkBufferCreateInfo &createInfo, memory::MemoryUsage memFlags)
{
    return m_allocator->createBuffer(m_handle, m_physicalDevice, createInfo, memFlags);
}

void Device::destroyBuffer(allocators::AllocatedBuffer &buffer)
{
    m_allocator->destroyBuffer(m_handle, buffer);
}

void Device::bindBufferMemory(const allocators::AllocatedBuffer &buffer, VkDeviceSize memoryOffset)
{
    m_allocator->bindBufferMemory(m_handle, buffer, memoryOffset);
}

VkDeviceSize Device::getTotalAllocatedVRAM() const
{
    return m_allocator->getTotalAllocatedVRAM();
}

void Device::clean()
{
    m_allocator->clean();

    if (m_handle)
    {
        vkDestroyDevice(m_handle, nullptr);
        m_handle = VK_NULL_HANDLE;
    }

    if (m_isRAMFileOpened)
    {
        fclose(m_ramFile);
        m_isRAMFileOpened = false;
    }
}

ELIX_NESTED_NAMESPACE_END