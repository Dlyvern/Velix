#include "Core/CommandBuffer.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/VulkanAssert.hpp"

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

    VX_VK_CHECK(vkAllocateCommandBuffers(VulkanContext::getContext()->getDevice(), &allocateInfo, &m_handle));

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
    VX_VK_TRY(vkResetCommandBuffer(m_handle, flags));
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

    if (VX_VK_TRY(vkQueueSubmit(queue, 1, &submitInfo, fence)) != VK_SUCCESS)
        return false;

    return true;
}

bool CommandBuffer::begin(VkCommandBufferUsageFlags flags, VkCommandBufferInheritanceInfo *inheritance)
{
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.pInheritanceInfo = inheritance;
    beginInfo.flags = flags;

    if (VX_VK_TRY(vkBeginCommandBuffer(m_handle, &beginInfo)) != VK_SUCCESS)
        return false;

    return true;
}

bool CommandBuffer::end()
{
    if (VX_VK_TRY(vkEndCommandBuffer(m_handle)) != VK_SUCCESS)
        return false;

    return true;
}

ELIX_NESTED_NAMESPACE_END
