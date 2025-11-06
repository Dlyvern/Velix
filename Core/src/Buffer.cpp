#include "Core/Buffer.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/VulkanHelpers.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

//TODO maybe throw here is not a good idea
Buffer::Buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memFlags, VkBufferCreateFlags flags) :
m_device(core::VulkanContext::getContext()->getDevice()), m_size(size)
{
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.flags = flags;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if(vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_buffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to create buffer");

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, m_buffer, &memRequirements);

    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = memRequirements.size;
    allocateInfo.memoryTypeIndex = helpers::findMemoryType(core::VulkanContext::getContext()->getPhysicalDevice(), memRequirements.memoryTypeBits, memFlags);

    if(vkAllocateMemory(m_device, &allocateInfo, nullptr, &m_bufferMemory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate buffer memory");

    bind(0);
}

void Buffer::map(void*& data, VkDeviceSize offset, VkMemoryMapFlags flags)
{
    vkMapMemory(m_device, m_bufferMemory, offset, m_size, flags, &data);
}

void Buffer::map(VkDeviceSize offset, VkDeviceSize size,  VkMemoryMapFlags flags, void*& data)
{
    vkMapMemory(m_device, m_bufferMemory, offset, size, flags, &data);
}

void Buffer::unmap()
{
    vkUnmapMemory(m_device, m_bufferMemory);
}

void Buffer::upload(const void* data, VkDeviceSize size)
{
    void* dst;
    map(0, size, 0, dst);
    std::memcpy(dst, data, static_cast<size_t>(size));
    unmap();
}

CommandBuffer::SharedPtr Buffer::copy(Buffer::SharedPtr srcBuffer,  Buffer::SharedPtr dstBuffer, CommandPool::SharedPtr commandPool, VkDeviceSize size)
{
    auto commandBuffer = CommandBuffer::create(VulkanContext::getContext()->getDevice(), commandPool->vk());
    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkBufferCopy copyRegion{
        .srcOffset = 0,
        .dstOffset = 0,
        .size = size
    };

    vkCmdCopyBuffer(commandBuffer->vk(), srcBuffer->vkBuffer(), dstBuffer->vkBuffer(), 1, &copyRegion);

    commandBuffer->end();

    return commandBuffer;
}

void Buffer::bind(VkDeviceSize memoryOffset)
{
    vkBindBufferMemory(m_device, m_buffer, m_bufferMemory, memoryOffset);
}

VkBuffer Buffer::vkBuffer()
{
    return m_buffer;
}

void Buffer::destroyVk()
{
    if(m_buffer)
    {
        vkDestroyBuffer(m_device, m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
    }
    
    if(m_bufferMemory)
    {
        vkFreeMemory(m_device, m_bufferMemory, nullptr);
        m_bufferMemory = VK_NULL_HANDLE;
    }
}

Buffer::SharedPtr Buffer::createCopied(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memFlags, CommandPool::SharedPtr commandPool, VkQueue queue)
{
    auto staging = Buffer::create(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging->upload(data, size);

    auto gpuBuffer = Buffer::create(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memFlags);

    auto cmd = Buffer::copy(staging, gpuBuffer, commandPool, size);
    cmd->submit(queue, {}, {}, {}, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    return gpuBuffer;
}

VkDeviceMemory Buffer::vkDeviceMemory()
{
    return m_bufferMemory;
}

std::shared_ptr<Buffer> Buffer::create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memFlags, VkBufferCreateFlags flags)
{
    return std::make_shared<Buffer>(size, usage, memFlags, flags);
}

Buffer::~Buffer()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END