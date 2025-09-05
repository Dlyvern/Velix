#include "Core/Buffer.hpp"
#include "Core/VulkanContext.hpp"
#include <iostream>
#include <cstring>

ELIX_NESTED_NAMESPACE_BEGIN(core)

//TODO maybe throw here is not a good idea
Buffer::Buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.flags = flags;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if(vkCreateBuffer(VulkanContext::getContext()->getDevice(), &bufferInfo, nullptr, &m_buffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to create buffer");

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(VulkanContext::getContext()->getDevice(), m_buffer, &memRequirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memRequirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, flags);

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

VkBuffer Buffer::vkBuffer()
{
    return m_buffer;
}

VkDeviceMemory Buffer::vkDeviceMemory()
{
    return m_bufferMemory;
}

uint32_t Buffer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(VulkanContext::getContext()->getPhysicalDevice(), &memoryProperties);

    for(uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
        if((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & flags) == flags)
            return i;

    throw std::runtime_error("Failed to find suitable memory type for buffer");
}

std::shared_ptr<Buffer> Buffer::create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags)
{
    return std::make_shared<Buffer>(size, usage, flags);
}

Buffer::~Buffer()
{
    if(m_buffer)
        vkDestroyBuffer(VulkanContext::getContext()->getDevice(), m_buffer, nullptr);
    
    if(m_bufferMemory)
        vkFreeMemory(VulkanContext::getContext()->getDevice(), m_bufferMemory, nullptr);
}

ELIX_NESTED_NAMESPACE_END