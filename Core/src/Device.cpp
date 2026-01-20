#include "Core/Device.hpp"
#include "Core/Memory/DefaultAllocator.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)

Device::Device(VkDevice device, VkPhysicalDevice physicalDevice, std::unique_ptr<allocators::IAllocator> allocator)
    : m_handle(device), m_physicalDevice(physicalDevice)
{
    if (!allocator)
        allocator = std::make_unique<allocators::DefaultAllocator>();

    m_allocator = std::move(allocator);
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

void Device::clean()
{
    m_allocator->clean();

    if (m_handle)
    {
        vkDestroyDevice(m_handle, nullptr);
        m_handle = VK_NULL_HANDLE;
    }
}

ELIX_NESTED_NAMESPACE_END