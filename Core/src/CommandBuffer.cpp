#include "Core/CommandBuffer.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/VulkanAssert.hpp"
#include "Core/VulkanHelpers.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)

CommandBuffer::CommandBuffer(CommandPool &commandPool, VkCommandBufferLevel level)
{
    createVk(commandPool, level);
}

void CommandBuffer::createVk(CommandPool &commandPool, VkCommandBufferLevel level)
{
    ELIX_VK_CREATE_GUARD()
    m_commandPool = &commandPool;

    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandBufferCount = 1;
    allocateInfo.pNext = nullptr;
    allocateInfo.commandPool = m_commandPool->vk();
    allocateInfo.level = level;

    VX_VK_CHECK(vkAllocateCommandBuffers(VulkanContext::getContext()->getDevice(), &allocateInfo, &m_handle));

    ELIX_VK_CREATE_GUARD_DONE()
}

void CommandBuffer::destroyVkImpl()
{
    if (m_handle)
    {
        reset();
        if (m_commandPool)
            vkFreeCommandBuffers(VulkanContext::getContext()->getDevice(), m_commandPool->vk(), 1, &m_handle);
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
    if (waitSemaphores.size() != waitStages.size())
    {
        VX_CHECK_MSG(false, "CommandBuffer::submit wait semaphore count (%zu) must match wait stage count (%zu)",
                     waitSemaphores.size(), waitStages.size());
        return false;
    }

    std::vector<SemaphoreSubmitDesc> waitSubmitInfos;
    waitSubmitInfos.reserve(waitSemaphores.size());
    for (size_t i = 0; i < waitSemaphores.size(); ++i)
    {
        waitSubmitInfos.push_back(SemaphoreSubmitDesc{
            .semaphore = waitSemaphores[i],
            .stageMask = static_cast<VkPipelineStageFlags2>(waitStages[i]),
        });
    }

    std::vector<SemaphoreSubmitDesc> signalSubmitInfos;
    signalSubmitInfos.reserve(signalSemaphores.size());
    for (VkSemaphore signalSemaphore : signalSemaphores)
    {
        signalSubmitInfos.push_back(SemaphoreSubmitDesc{
            .semaphore = signalSemaphore,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        });
    }

    return submit2(queue, waitSubmitInfos, signalSubmitInfos, fence);
}

bool CommandBuffer::submit2(VkQueue queue,
                            const std::vector<SemaphoreSubmitDesc> &waitSemaphores,
                            const std::vector<SemaphoreSubmitDesc> &signalSemaphores,
                            VkFence fence,
                            uint32_t deviceMask)
{
    VkCommandBufferSubmitInfo commandBufferInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandBufferInfo.commandBuffer = m_handle;
    commandBufferInfo.deviceMask = deviceMask;

    std::vector<VkSemaphoreSubmitInfo> waitSemaphoreInfos;
    waitSemaphoreInfos.reserve(waitSemaphores.size());
    for (const auto &waitSemaphore : waitSemaphores)
    {
        VkSemaphoreSubmitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        info.semaphore = waitSemaphore.semaphore;
        info.value = waitSemaphore.value;
        info.stageMask = waitSemaphore.stageMask;
        info.deviceIndex = waitSemaphore.deviceIndex;
        waitSemaphoreInfos.push_back(info);
    }

    std::vector<VkSemaphoreSubmitInfo> signalSemaphoreInfos;
    signalSemaphoreInfos.reserve(signalSemaphores.size());
    for (const auto &signalSemaphore : signalSemaphores)
    {
        VkSemaphoreSubmitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        info.semaphore = signalSemaphore.semaphore;
        info.value = signalSemaphore.value;
        info.stageMask = signalSemaphore.stageMask;
        info.deviceIndex = signalSemaphore.deviceIndex;
        signalSemaphoreInfos.push_back(info);
    }

    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitSemaphoreInfos.size());
    submitInfo.pWaitSemaphoreInfos = waitSemaphoreInfos.empty() ? nullptr : waitSemaphoreInfos.data();
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandBufferInfo;
    submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalSemaphoreInfos.size());
    submitInfo.pSignalSemaphoreInfos = signalSemaphoreInfos.empty() ? nullptr : signalSemaphoreInfos.data();

    // Queue operations on the same VkQueue require external host synchronization.
    std::lock_guard<std::mutex> submitLock(helpers::queueHostSyncMutex());
    if (VX_VK_TRY(vkQueueSubmit2(queue, 1, &submitInfo, fence)) != VK_SUCCESS)
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
