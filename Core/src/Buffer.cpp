#include "Core/Buffer.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/VulkanHelpers.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

//TODO maybe throw here is not a good idea
Buffer::Buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, VkMemoryPropertyFlags memFlags)
{
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.flags = flags;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if(vkCreateBuffer(VulkanContext::getContext()->getDevice(), &bufferInfo, nullptr, &m_buffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to create buffer");

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(VulkanContext::getContext()->getDevice(), m_buffer, &memRequirements);

    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = memRequirements.size;
    allocateInfo.memoryTypeIndex = helpers::findMemoryType(memRequirements.memoryTypeBits, memFlags);

    if(vkAllocateMemory(VulkanContext::getContext()->getDevice(), &allocateInfo, nullptr, &m_bufferMemory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate buffer memory");

    vkBindBufferMemory(VulkanContext::getContext()->getDevice(), m_buffer, m_bufferMemory, 0);
}

void Buffer::upload(const void* data, VkDeviceSize size)
{
    void* dst;
    vkMapMemory(VulkanContext::getContext()->getDevice(), m_bufferMemory, 0, size, 0, &dst);
    std::memcpy(dst, data, static_cast<size_t>(size));
    vkUnmapMemory(VulkanContext::getContext()->getDevice(), m_bufferMemory);
}

CommandBuffer::SharedPtr Buffer::copy(Buffer::SharedPtr srcBuffer,  Buffer::SharedPtr dstBuffer, CommandPool::SharedPtr commandPool, VkDeviceSize size)
{
    auto commandBuffer = CommandBuffer::create(VulkanContext::getContext()->getDevice(), commandPool->vk());
    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;

    vkCmdCopyBuffer(commandBuffer->vk(), srcBuffer->vkBuffer(), dstBuffer->vkBuffer(), 1, &copyRegion);

    commandBuffer->end();

    return commandBuffer;
}

void Buffer::bind(VkDeviceSize memoryOffset)
{
    vkBindBufferMemory(VulkanContext::getContext()->getDevice(), m_buffer, m_bufferMemory, memoryOffset);
}

VkBuffer Buffer::vkBuffer()
{
    return m_buffer;
}

void Buffer::destroy()
{
    if(m_buffer)
        vkDestroyBuffer(VulkanContext::getContext()->getDevice(), m_buffer, nullptr);
    
    if(m_bufferMemory)
        vkFreeMemory(VulkanContext::getContext()->getDevice(), m_bufferMemory, nullptr);

    m_isDestroyed = true;
}

Buffer::SharedPtr Buffer::createCopied(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, VkMemoryPropertyFlags memFlags, CommandPool::SharedPtr commandPool, VkQueue queue)
{
    auto staging = Buffer::create(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging->upload(data, size);

    auto gpuBuffer = Buffer::create(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, flags, memFlags);

    auto cmd = Buffer::copy(staging, gpuBuffer, commandPool, size);
    cmd->submit(queue, {}, {}, {}, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    return gpuBuffer;
}

VkDeviceMemory Buffer::vkDeviceMemory()
{
    return m_bufferMemory;
}

std::shared_ptr<Buffer> Buffer::create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, VkMemoryPropertyFlags memFlags)
{
    return std::make_shared<Buffer>(size, usage, flags, memFlags);
}

Buffer::~Buffer()
{
    if(!m_isDestroyed)
        destroy();
}

ELIX_NESTED_NAMESPACE_END