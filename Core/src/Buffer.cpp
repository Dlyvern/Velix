#include "Core/Buffer.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/VulkanHelpers.hpp"
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
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    m_bufferAllocation = core::VulkanContext::getContext()->getDevice()->createBuffer(bufferInfo, memFlags);
    m_handle = m_bufferAllocation.buffer;

    ELIX_VK_CREATE_GUARD_DONE()
}

void Buffer::map(void *&data, VkDeviceSize offset, VkMemoryMapFlags flags)
{
    map(data, m_size, offset, flags);
}

void Buffer::map(void *&data, VkDeviceSize size, VkDeviceSize offset, VkMemoryMapFlags flags)
{
    core::VulkanContext::getContext()->getDevice()->mapMemory(m_bufferAllocation.allocation, offset, size, flags, data);
}

void Buffer::unmap()
{
    core::VulkanContext::getContext()->getDevice()->unmapMemory(m_bufferAllocation.allocation);
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
    core::VulkanContext::getContext()->getDevice()->destroyBuffer(m_bufferAllocation);
    m_handle = m_bufferAllocation.buffer;
}

CommandBuffer Buffer::copy(Ptr srcBuffer, Ptr dstBuffer, CommandPool::SharedPtr commandPool, VkDeviceSize size)
{
    auto commandBuffer = CommandBuffer::create(commandPool);
    commandBuffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkBufferCopy copyRegion{
        .srcOffset = 0,
        .dstOffset = 0,
        .size = size};

    vkCmdCopyBuffer(commandBuffer, *srcBuffer, *dstBuffer, 1, &copyRegion);

    commandBuffer.end();

    return commandBuffer;
}

void Buffer::copyImageToBuffer(VkImage image, VkOffset3D imageOffset)
{
    auto commandPool = core::VulkanContext::getContext()->getTransferCommandPool();
    auto queue = core::VulkanContext::getContext()->getTransferQueue();

    auto commandBuffer = CommandBuffer::create(commandPool);
    commandBuffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // TODO change it to depth aspect(pass core::Image instead of raw vulkan)
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = imageOffset;
    region.imageExtent = {1, 1, 1};

    vkCmdCopyImageToBuffer(
        commandBuffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        m_handle,
        1,
        &region);

    commandBuffer.end();

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = 0;

    VkFence fence = VK_NULL_HANDLE;

    if (vkCreateFence(core::VulkanContext::getContext()->getDevice(), &fenceInfo, nullptr, &fence) != VK_SUCCESS)
        std::cerr << "Failed to create fence for copy image to buffer. Falling back to vkQueueWaitIdle\n";

    if (!commandBuffer.submit(queue, {}, {}, {}, fence))
        std::cerr << "Failed to submit copy image to buffer\n";

    if (fence)
    {
        vkWaitForFences(core::VulkanContext::getContext()->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(core::VulkanContext::getContext()->getDevice(), fence, nullptr);
    }
    else
        vkQueueWaitIdle(queue);
}

Buffer::SharedPtr Buffer::createCopied(const void *data, VkDeviceSize size, VkBufferUsageFlags usage, memory::MemoryUsage memFlags, CommandPool::SharedPtr commandPool)
{
    auto queue = core::VulkanContext::getContext()->getTransferQueue();

    if (!commandPool)
        commandPool = core::VulkanContext::getContext()->getTransferCommandPool();

    auto staging = Buffer::create(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memory::MemoryUsage::CPU_TO_GPU);
    staging.upload(data, size);

    auto gpuBuffer = Buffer::createShared(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memFlags);

    auto cmd = Buffer::copy(&staging, gpuBuffer.get(), commandPool, size);

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = 0;

    VkFence fence = VK_NULL_HANDLE;

    if (vkCreateFence(core::VulkanContext::getContext()->getDevice(), &fenceInfo, nullptr, &fence) != VK_SUCCESS)
        std::cerr << "Failed to create fence for buffer copy. Falling back to vkQueueWaitIdle\n";

    if (!cmd.submit(queue, {}, {}, {}, fence))
        std::cerr << "Failed to submit buffer copy\n";

    if (fence)
    {
        vkWaitForFences(core::VulkanContext::getContext()->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(core::VulkanContext::getContext()->getDevice(), fence, nullptr);
    }
    else
        vkQueueWaitIdle(queue);

    return gpuBuffer;
}

Buffer::~Buffer()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END