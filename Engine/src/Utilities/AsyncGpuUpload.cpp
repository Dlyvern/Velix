#include "Engine/Utilities/AsyncGpuUpload.hpp"

#include "Core/Logger.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/VulkanHelpers.hpp"

#include <algorithm>
#include <mutex>
#include <vector>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(utilities)

namespace
{
    struct PendingUpload
    {
        VkFence fence{VK_NULL_HANDLE};
        uint64_t completionValue{0u};
        std::vector<core::CommandBuffer::SharedPtr> commandBuffers;
        std::vector<core::Buffer::SharedPtr> stagingBuffers;
    };

    // Queued-but-not-yet-submitted uploads (for batchFlush).
    struct BatchEntry
    {
        core::CommandBuffer::SharedPtr commandBuffer;
        std::vector<core::Buffer::SharedPtr> stagingBuffers;
    };

    std::mutex g_pendingUploadsMutex;
    std::vector<PendingUpload> g_pendingUploads;
    std::vector<VkSemaphore> g_readySemaphores;
    VkSemaphore g_timelineSemaphore{VK_NULL_HANDLE};
    uint64_t g_nextTimelineValue{1u};
    uint64_t g_readyTimelineValue{0u};

    std::mutex g_batchMutex;
    std::vector<BatchEntry> g_batchQueue;

    bool ensureTimelineSemaphoreLocked(VkDevice device)
    {
        if (g_timelineSemaphore != VK_NULL_HANDLE)
            return true;

        auto context = core::VulkanContext::getContext();
        if (!context || !context->hasTimelineSemaphoreSupport())
            return false;

        VkSemaphoreTypeCreateInfo typeCreateInfo{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        typeCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        typeCreateInfo.initialValue = 0u;

        VkSemaphoreCreateInfo semaphoreCreateInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        semaphoreCreateInfo.pNext = &typeCreateInfo;

        return vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &g_timelineSemaphore) == VK_SUCCESS;
    }
} // namespace

VkFence AsyncGpuUpload::submitAsync(core::CommandBuffer::SharedPtr commandBuffer, VkQueue queue)
{
    if (!commandBuffer || !queue)
        return VK_NULL_HANDLE;

    auto context = core::VulkanContext::getContext();
    if (!context)
        return VK_NULL_HANDLE;

    VkFenceCreateInfo fenceCreateInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};

    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(context->getDevice(), &fenceCreateInfo, nullptr, &fence) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    if (!commandBuffer->submit(queue, {}, {}, {}, fence))
    {
        vkDestroyFence(context->getDevice(), fence, nullptr);
        return VK_NULL_HANDLE;
    }

    return fence;
}

bool AsyncGpuUpload::submitAndWait(core::CommandBuffer::SharedPtr commandBuffer, VkQueue queue)
{
    if (!commandBuffer || queue == VK_NULL_HANDLE)
        return false;

    auto context = core::VulkanContext::getContext();
    if (!context)
        return false;

    VkFenceCreateInfo fenceCreateInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(context->getDevice(), &fenceCreateInfo, nullptr, &fence) != VK_SUCCESS)
        return false;

    const bool submitted = commandBuffer->submit(queue, {}, {}, {}, fence);
    const bool waited = submitted && vkWaitForFences(context->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;

    vkDestroyFence(context->getDevice(), fence, nullptr);
    return submitted && waited;
}

bool AsyncGpuUpload::submit(core::CommandBuffer::SharedPtr commandBuffer, VkQueue queue,
                            std::vector<core::Buffer::SharedPtr> stagingBuffers)
{
    if (!commandBuffer || queue == VK_NULL_HANDLE)
        return false;

    VkDevice device = core::VulkanContext::getContext()->getDevice();

    VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
    uint64_t completionValue = 0u;
    {
        std::lock_guard<std::mutex> lock(g_pendingUploadsMutex);
        if (ensureTimelineSemaphoreLocked(device))
        {
            timelineSemaphore = g_timelineSemaphore;
            completionValue = g_nextTimelineValue++;
        }
    }

    if (timelineSemaphore != VK_NULL_HANDLE)
    {
        const bool submitted = commandBuffer->submit2(
            queue,
            {},
            {core::CommandBuffer::SemaphoreSubmitDesc{
                .semaphore = timelineSemaphore,
                .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .value = completionValue,
            }});

        if (!submitted)
            return false;

        {
            std::lock_guard<std::mutex> lock(g_pendingUploadsMutex);
            g_readyTimelineValue = std::max(g_readyTimelineValue, completionValue);
            g_pendingUploads.push_back(PendingUpload{
                .completionValue = completionValue,
                .commandBuffers = {std::move(commandBuffer)},
                .stagingBuffers = std::move(stagingBuffers),
            });
        }

        return true;
    }

    VkFenceCreateInfo fenceCreateInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(device, &fenceCreateInfo, nullptr, &fence) != VK_SUCCESS)
        return false;

    VkSemaphoreCreateInfo semaphoreCreateInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore completionSemaphore = VK_NULL_HANDLE;
    if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &completionSemaphore) != VK_SUCCESS)
    {
        vkDestroyFence(device, fence, nullptr);
        return false;
    }

    if (!commandBuffer->submit(queue, {}, {}, {completionSemaphore}, fence))
    {
        vkDestroySemaphore(device, completionSemaphore, nullptr);
        vkDestroyFence(device, fence, nullptr);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_pendingUploadsMutex);
        if (completionSemaphore != VK_NULL_HANDLE)
            g_readySemaphores.push_back(completionSemaphore);

        g_pendingUploads.push_back(PendingUpload{
            .fence = fence,
            .commandBuffers = {std::move(commandBuffer)},
            .stagingBuffers = std::move(stagingBuffers),
        });
    }

    return true;
}

void AsyncGpuUpload::enqueue(core::CommandBuffer::SharedPtr commandBuffer,
                             std::vector<core::Buffer::SharedPtr> stagingBuffers)
{
    if (!commandBuffer)
        return;

    std::lock_guard<std::mutex> lock(g_batchMutex);
    g_batchQueue.push_back(BatchEntry{
        .commandBuffer = std::move(commandBuffer),
        .stagingBuffers = std::move(stagingBuffers),
    });
}

bool AsyncGpuUpload::batchFlush(VkQueue queue)
{
    std::vector<BatchEntry> batch;
    {
        std::lock_guard<std::mutex> lock(g_batchMutex);
        if (g_batchQueue.empty())
            return false;
        batch.swap(g_batchQueue);
    }

    VkDevice device = core::VulkanContext::getContext()->getDevice();

    std::vector<VkCommandBufferSubmitInfo> commandBufferInfos;
    commandBufferInfos.reserve(batch.size());
    for (const auto &entry : batch)
    {
        VkCommandBufferSubmitInfo commandBufferInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandBufferInfo.commandBuffer = entry.commandBuffer->vk();
        commandBufferInfo.deviceMask = 0u;
        commandBufferInfos.push_back(commandBufferInfo);
    }

    VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
    uint64_t completionValue = 0u;
    {
        std::lock_guard<std::mutex> lock(g_pendingUploadsMutex);
        if (ensureTimelineSemaphoreLocked(device))
        {
            timelineSemaphore = g_timelineSemaphore;
            completionValue = g_nextTimelineValue++;
        }
    }

    if (timelineSemaphore != VK_NULL_HANDLE)
    {
        VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signalInfo.semaphore = timelineSemaphore;
        signalInfo.value = completionValue;
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submitInfo.commandBufferInfoCount = static_cast<uint32_t>(commandBufferInfos.size());
        submitInfo.pCommandBufferInfos = commandBufferInfos.data();
        submitInfo.signalSemaphoreInfoCount = 1u;
        submitInfo.pSignalSemaphoreInfos = &signalInfo;

        {
            std::lock_guard<std::mutex> submitLock(core::helpers::queueHostSyncMutex());
            if (vkQueueSubmit2(queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
                return false;
        }

        std::vector<core::Buffer::SharedPtr> allStagingBuffers;
        std::vector<core::CommandBuffer::SharedPtr> allCommandBuffers;
        for (auto &entry : batch)
        {
            allCommandBuffers.push_back(std::move(entry.commandBuffer));
            for (auto &buf : entry.stagingBuffers)
                allStagingBuffers.push_back(std::move(buf));
        }

        {
            std::lock_guard<std::mutex> lock(g_pendingUploadsMutex);
            g_readyTimelineValue = std::max(g_readyTimelineValue, completionValue);
            g_pendingUploads.push_back(PendingUpload{
                .completionValue = completionValue,
                .commandBuffers = std::move(allCommandBuffers),
                .stagingBuffers = std::move(allStagingBuffers),
            });
        }

        return true;
    }

    VkFenceCreateInfo fenceCreateInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(device, &fenceCreateInfo, nullptr, &fence) != VK_SUCCESS)
        return false;

    VkSemaphoreCreateInfo semaphoreCreateInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore completionSemaphore = VK_NULL_HANDLE;
    if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &completionSemaphore) != VK_SUCCESS)
    {
        vkDestroyFence(device, fence, nullptr);
        return false;
    }

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    std::vector<VkCommandBuffer> rawCommandBuffers;
    rawCommandBuffers.reserve(commandBufferInfos.size());
    for (const auto &commandBufferInfo : commandBufferInfos)
        rawCommandBuffers.push_back(commandBufferInfo.commandBuffer);
    submitInfo.commandBufferCount = static_cast<uint32_t>(rawCommandBuffers.size());
    submitInfo.pCommandBuffers = rawCommandBuffers.data();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &completionSemaphore;

    {
        std::lock_guard<std::mutex> submitLock(core::helpers::queueHostSyncMutex());
        if (vkQueueSubmit(queue, 1, &submitInfo, fence) != VK_SUCCESS)
        {
            vkDestroySemaphore(device, completionSemaphore, nullptr);
            vkDestroyFence(device, fence, nullptr);
            return false;
        }
    }

    // Merge all staging buffers into a single PendingUpload entry so they are
    // kept alive until the fence signals.
    std::vector<core::Buffer::SharedPtr> allStagingBuffers;
    std::vector<core::CommandBuffer::SharedPtr> allCommandBuffers;
    for (auto &entry : batch)
    {
        allCommandBuffers.push_back(std::move(entry.commandBuffer));
        for (auto &buf : entry.stagingBuffers)
            allStagingBuffers.push_back(std::move(buf));
    }

    // Store a single PendingUpload that represents the whole batch so every
    // submitted command buffer stays alive until the fence signals.
    {
        std::lock_guard<std::mutex> lock(g_pendingUploadsMutex);
        if (completionSemaphore != VK_NULL_HANDLE)
            g_readySemaphores.push_back(completionSemaphore);

        g_pendingUploads.push_back(PendingUpload{
            .fence = fence,
            .commandBuffers = std::move(allCommandBuffers),
            .stagingBuffers = std::move(allStagingBuffers),
        });
    }

    return true;
}

void AsyncGpuUpload::collectFinished(VkDevice device)
{
    std::lock_guard<std::mutex> lock(g_pendingUploadsMutex);

    uint64_t completedTimelineValue = 0u;
    if (g_timelineSemaphore != VK_NULL_HANDLE)
    {
        if (vkGetSemaphoreCounterValue(device, g_timelineSemaphore, &completedTimelineValue) != VK_SUCCESS)
            completedTimelineValue = 0u;
    }

    size_t writeIndex = 0;
    for (size_t readIndex = 0; readIndex < g_pendingUploads.size(); ++readIndex)
    {
        auto &pendingUpload = g_pendingUploads[readIndex];

        if (pendingUpload.completionValue > 0u)
        {
            if (completedTimelineValue < pendingUpload.completionValue)
            {
                if (writeIndex != readIndex)
                    g_pendingUploads[writeIndex] = std::move(pendingUpload);
                ++writeIndex;
            }
            continue;
        }

        VkResult status = vkGetFenceStatus(device, pendingUpload.fence);
        if (status == VK_NOT_READY)
        {
            if (writeIndex != readIndex)
                g_pendingUploads[writeIndex] = std::move(pendingUpload);
            ++writeIndex;
            continue;
        }

        vkDestroyFence(device, pendingUpload.fence, nullptr);

        if (status == VK_SUCCESS)
        {
            // The completion semaphore was already published when the upload was
            // submitted so same-frame render work can wait on it immediately.
        }
        else
        {
            VX_ENGINE_ERROR_STREAM("Failed to poll upload fence status: "
                                   << core::helpers::vulkanResultToString(status) << '\n');
        }
    }

    g_pendingUploads.resize(writeIndex);
}

AsyncGpuUpload::TimelineWait AsyncGpuUpload::acquireReadyTimelineWait()
{
    std::lock_guard<std::mutex> lock(g_pendingUploadsMutex);

    if (g_timelineSemaphore == VK_NULL_HANDLE || g_readyTimelineValue == 0u)
        return {};

    TimelineWait wait{
        .semaphore = g_timelineSemaphore,
        .value = g_readyTimelineValue,
    };
    g_readyTimelineValue = 0u;
    return wait;
}

std::vector<VkSemaphore> AsyncGpuUpload::acquireReadySemaphores()
{
    std::lock_guard<std::mutex> lock(g_pendingUploadsMutex);

    std::vector<VkSemaphore> readySemaphores;
    readySemaphores.swap(g_readySemaphores);

    return readySemaphores;
}

void AsyncGpuUpload::releaseSemaphores(VkDevice device, const std::vector<VkSemaphore> &semaphores)
{
    for (const VkSemaphore semaphore : semaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(device, semaphore, nullptr);
    }
}

void AsyncGpuUpload::flush(VkDevice device)
{
    // Drain any enqueued-but-not-yet-submitted batch entries first.
    {
        std::lock_guard<std::mutex> lock(g_batchMutex);
        g_batchQueue.clear();
    }

    std::vector<PendingUpload> pendingUploads;
    std::vector<VkSemaphore> readySemaphores;
    VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
    uint64_t maxTimelineValue = 0u;
    {
        std::lock_guard<std::mutex> lock(g_pendingUploadsMutex);
        pendingUploads.swap(g_pendingUploads);
        readySemaphores.swap(g_readySemaphores);
        timelineSemaphore = g_timelineSemaphore;
        g_readyTimelineValue = 0u;
    }

    for (const auto &pendingUpload : pendingUploads)
        maxTimelineValue = std::max(maxTimelineValue, pendingUpload.completionValue);

    if (timelineSemaphore != VK_NULL_HANDLE && maxTimelineValue > 0u)
    {
        VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        waitInfo.semaphoreCount = 1u;
        waitInfo.pSemaphores = &timelineSemaphore;
        waitInfo.pValues = &maxTimelineValue;
        if (vkWaitSemaphores(device, &waitInfo, UINT64_MAX) != VK_SUCCESS)
            VX_ENGINE_ERROR_STREAM("Failed to wait for upload timeline during flush\n");
    }

    for (auto &pendingUpload : pendingUploads)
    {
        if (pendingUpload.completionValue > 0u || pendingUpload.fence == VK_NULL_HANDLE)
            continue;

        if (vkWaitForFences(device, 1, &pendingUpload.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
            VX_ENGINE_ERROR_STREAM("Failed to wait for upload fence during flush\n");

        vkDestroyFence(device, pendingUpload.fence, nullptr);
    }

    releaseSemaphores(device, readySemaphores);
}

void AsyncGpuUpload::shutdown(VkDevice device)
{
    flush(device);

    VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> lock(g_pendingUploadsMutex);
        timelineSemaphore = g_timelineSemaphore;
        g_timelineSemaphore = VK_NULL_HANDLE;
        g_nextTimelineValue = 1u;
        g_readyTimelineValue = 0u;
    }

    if (timelineSemaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(device, timelineSemaphore, nullptr);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
