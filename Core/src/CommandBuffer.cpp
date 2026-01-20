#include "Core/CommandBuffer.hpp"
#include <iostream>
#include <string>
#include <stdexcept>
#include "Core/VulkanContext.hpp"
#include "Core/VulkanHelpers.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)

CommandBuffer::CommandBuffer(CommandPool::SharedPtr commandPool, VkCommandBufferLevel level)
{
    createVk(commandPool, level);
}

void CommandBuffer::createVk(CommandPool::SharedPtr commandPool, VkCommandBufferLevel level)
{
    ELIX_VK_CREATE_GUARD()
    m_commandPool = commandPool;

    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandBufferCount = 1;
    allocateInfo.pNext = nullptr;
    allocateInfo.commandPool = m_commandPool.lock()->vk();
    allocateInfo.level = level;

    if (VkResult result = vkAllocateCommandBuffers(VulkanContext::getContext()->getDevice(), &allocateInfo, &m_handle);
        result != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate memory for command buffer: " + std::to_string(result));

    ELIX_VK_CREATE_GUARD_DONE()
}

void CommandBuffer::destroyVkImpl()
{
    if (m_handle)
    {
        reset();
        vkFreeCommandBuffers(VulkanContext::getContext()->getDevice(), m_commandPool.lock()->vk(), 1, &m_handle);
        m_handle = VK_NULL_HANDLE;
    }
}

CommandBuffer::~CommandBuffer()
{
    destroyVk();
}

void CommandBuffer::reset(VkCommandBufferResetFlags flags)
{
    vkResetCommandBuffer(m_handle, flags);
}

bool CommandBuffer::submit(VkQueue queue, const std::vector<VkSemaphore> &waitSemaphores, const std::vector<VkPipelineStageFlags> &waitStages,
                           const std::vector<VkSemaphore> &signalSemaphores, VkFence fence)
{
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = waitSemaphores.size();
    submitInfo.pWaitSemaphores = waitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_handle;
    submitInfo.signalSemaphoreCount = signalSemaphores.size();
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    if (VkResult result = vkQueueSubmit(queue, 1, &submitInfo, fence); result != VK_SUCCESS)
    {
        std::cerr << "Failed to submit command buffer: " + helpers::vulkanResultToString(result) << std::endl;
        return false;
    }

    return true;
}

bool CommandBuffer::begin(VkCommandBufferUsageFlags flags, VkCommandBufferInheritanceInfo *inheritance)
{
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.pInheritanceInfo = inheritance;
    beginInfo.flags = flags;

    if (VkResult result = vkBeginCommandBuffer(m_handle, &beginInfo); result != VK_SUCCESS)
    {
        std::cerr << "Failed to begin command buffer: " + helpers::vulkanResultToString(result) << std::endl;
        return false;
    }

    return true;
}

bool CommandBuffer::end()
{
    if (VkResult result = vkEndCommandBuffer(m_handle); result != VK_SUCCESS)
    {
        std::cerr << "Failed to end command buffer: " + helpers::vulkanResultToString(result) << std::endl;
        return false;
    }

    return true;
}

ELIX_NESTED_NAMESPACE_END