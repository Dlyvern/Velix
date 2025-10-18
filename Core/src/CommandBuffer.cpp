#include "Core/CommandBuffer.hpp"
#include <iostream>
#include <string>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

CommandBuffer::CommandBuffer(VkDevice device, VkCommandPool commandPool, VkCommandBufferLevel level) : m_commandPool(commandPool), m_device(device)
{
    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandBufferCount = 1;
    allocateInfo.pNext = nullptr;
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = level;

    if(VkResult result = vkAllocateCommandBuffers(m_device, &allocateInfo, &m_commandBuffer); result != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate memory for command buffer: " + std::to_string(result));
}

CommandBuffer::SharedPtr CommandBuffer::create(VkDevice device, VkCommandPool commandPool, VkCommandBufferLevel level)
{
    return std::make_shared<CommandBuffer>(device, commandPool, level);
}

void CommandBuffer::destroyVk()
{
    if(m_commandBuffer)
    {
        reset();
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &m_commandBuffer);
        m_commandBuffer = VK_NULL_HANDLE;
    }
}

CommandBuffer::~CommandBuffer()
{
    destroyVk();
}

void CommandBuffer::reset(VkCommandBufferResetFlags flags)
{
    vkResetCommandBuffer(m_commandBuffer, flags);
}

void CommandBuffer::submit(VkQueue queue, const std::vector<VkSemaphore>& waitSemaphores, const std::vector<VkPipelineStageFlags>& waitStages,
const std::vector<VkSemaphore>& signalSemaphores, VkFence fence)
{
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = waitSemaphores.size();
    submitInfo.pWaitSemaphores = waitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;
    submitInfo.signalSemaphoreCount = signalSemaphores.size();
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    if(vkQueueSubmit(queue, 1, &submitInfo, fence) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit draw command buffer");
}

VkCommandBuffer CommandBuffer::vk()
{
    return m_commandBuffer;
}

VkCommandBuffer* CommandBuffer::pVk()
{
    return &m_commandBuffer;
}

bool CommandBuffer::begin(VkCommandBufferUsageFlags flags, VkCommandBufferInheritanceInfo* inheritance)
{
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.pInheritanceInfo = inheritance;
    beginInfo.flags = flags;

    if(VkResult result = vkBeginCommandBuffer(m_commandBuffer, &beginInfo); result != VK_SUCCESS)
    {
        std::cerr << ("Failed to begin command buffer: " + std::to_string(result)) << std::endl;
        return false;
    }

    return true;
}

bool CommandBuffer::end()
{
    if(VkResult result = vkEndCommandBuffer(m_commandBuffer); result != VK_SUCCESS)
    {
        std::cerr << ("Failed to end command buffer: " + std::to_string(result)) << std::endl;
        return false;
    }

    return true;
}

ELIX_NESTED_NAMESPACE_END