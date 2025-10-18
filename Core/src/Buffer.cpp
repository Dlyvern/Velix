#include "Core/Buffer.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/VulkanHelpers.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

//TODO maybe throw here is not a good idea
Buffer::Buffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, VkMemoryPropertyFlags memFlags) :
m_device(device)
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
    allocateInfo.memoryTypeIndex = helpers::findMemoryType(physicalDevice, memRequirements.memoryTypeBits, memFlags);

    if(vkAllocateMemory(m_device, &allocateInfo, nullptr, &m_bufferMemory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate buffer memory");

    vkBindBufferMemory(m_device, m_buffer, m_bufferMemory, 0);
}

void Buffer::map(VkDeviceSize offset, VkDeviceSize size,  VkMemoryMapFlags flags, void* data)
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
    // map(0, size, 0, dst);
    vkMapMemory(m_device, m_bufferMemory, 0, size, 0, &dst);
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

void Buffer::destroy()
{
    if(m_buffer)
        vkDestroyBuffer(m_device, m_buffer, nullptr);
    
    if(m_bufferMemory)
        vkFreeMemory(m_device, m_bufferMemory, nullptr);

    m_isDestroyed = true;
}

Buffer::SharedPtr Buffer::createCopied(VkDevice device, VkPhysicalDevice physicalDevice, const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, VkMemoryPropertyFlags memFlags, CommandPool::SharedPtr commandPool, VkQueue queue)
{
    auto staging = Buffer::create(device, physicalDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging->upload(data, size);

    auto gpuBuffer = Buffer::create(device, physicalDevice, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, flags, memFlags);

    auto cmd = Buffer::copy(staging, gpuBuffer, commandPool, size);
    cmd->submit(queue, {}, {}, {}, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    return gpuBuffer;
}

VkDeviceMemory Buffer::vkDeviceMemory()
{
    return m_bufferMemory;
}

std::shared_ptr<Buffer> Buffer::create(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, VkMemoryPropertyFlags memFlags)
{
    return std::make_shared<Buffer>(device, physicalDevice, size, usage, flags, memFlags);
}

Buffer::~Buffer()
{
    if(!m_isDestroyed)
        destroy();
}

ELIX_NESTED_NAMESPACE_END