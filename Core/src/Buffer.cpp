#include "Core/Buffer.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Core/Logger.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

Buffer::Buffer(VkDeviceSize size, VkBufferUsageFlags usage, memory::MemoryUsage memFlags, VkBufferCreateFlags flags) : m_size(size)
{
    createVk(size, usage, memFlags, flags);
}

void Buffer::createVk(VkDeviceSize size, VkBufferUsageFlags usage, memory::MemoryUsage memFlags, VkBufferCreateFlags flags)
{
    ELIX_VK_CREATE_GUARD()

    m_size = size;

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.flags = flags;
    bufferInfo.size = m_size;
    bufferInfo.usage = usage;

    auto context = core::VulkanContext::getContext();
    const uint32_t graphicsFamily = context->getGraphicsFamily();
    const uint32_t transferFamily = context->getTransferFamily();

    uint32_t queueFamilies[] = {graphicsFamily, transferFamily};
    if (graphicsFamily != transferFamily)
    {
        bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        bufferInfo.queueFamilyIndexCount = 2;
        bufferInfo.pQueueFamilyIndices = queueFamilies;
    }
    else
    {
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.queueFamilyIndexCount = 0;
        bufferInfo.pQueueFamilyIndices = nullptr;
    }

    m_bufferAllocation = core::VulkanContext::getContext()->getDevice()->createBuffer(bufferInfo, memFlags);
    m_handle = m_bufferAllocation.buffer;

    ELIX_VK_CREATE_GUARD_DONE()
}

void Buffer::map(void *&data, VkDeviceSize offset, VkMemoryMapFlags flags)
{
    map(data, m_size, offset, flags);
    m_isMapped = true;
}

void Buffer::map(void *&data, VkDeviceSize size, VkDeviceSize offset, VkMemoryMapFlags flags)
{
    core::VulkanContext::getContext()->getDevice()->mapMemory(m_bufferAllocation.allocation, offset, size, flags, data);
    m_isMapped = true;
}

void Buffer::unmap()
{
    core::VulkanContext::getContext()->getDevice()->unmapMemory(m_bufferAllocation.allocation);
    m_isMapped = false;
}

void Buffer::upload(const void *data, VkDeviceSize size, VkDeviceSize offset)
{
    void *dst = nullptr;
    map(dst, size, offset, 0);
    std::memcpy(dst, data, (size_t)size);
    unmap();
}

void Buffer::upload(const void *data, VkDeviceSize size)
{
    void *dst;
    map(dst, size, 0, 0);
    std::memcpy(dst, data, static_cast<size_t>(size));
    unmap();
}

void Buffer::upload(const void *data)
{
    upload(data, m_size);
}

void Buffer::bind(VkDeviceSize memoryOffset)
{
    core::VulkanContext::getContext()->getDevice()->bindBufferMemory(m_bufferAllocation, memoryOffset);
}

void Buffer::destroyVkImpl()
{
    if (m_handle)
    {
        if (m_isMapped)
            unmap();

        core::VulkanContext::getContext()->getDevice()->destroyBuffer(m_bufferAllocation);
        m_handle = m_bufferAllocation.buffer;
    }
}

Buffer::~Buffer()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END
