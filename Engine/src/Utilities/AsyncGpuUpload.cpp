#include "Engine/Utilities/AsyncGpuUpload.hpp"

#include "Core/Logger.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/VulkanHelpers.hpp"

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
        VkSemaphore completionSemaphore{VK_NULL_HANDLE};
        core::CommandBuffer::SharedPtr commandBuffer{nullptr};
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

    std::mutex g_batchMutex;
    std::vector<BatchEntry> g_batchQueue;
} // namespace

bool AsyncGpuUpload::submit(core::CommandBuffer::SharedPtr commandBuffer, VkQueue queue,
                            std::vector<core::Buffer::SharedPtr> stagingBuffers)
{
    if (!commandBuffer || queue == VK_NULL_HANDLE)
        return false;

    VkDevice device = core::VulkanContext::getContext()->getDevice();

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
        g_pendingUploads.push_back(PendingUpload{
            .fence = fence,
            .completionSemaphore = completionSemaphore,
            .commandBuffer = std::move(commandBuffer),
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

    // Collect raw VkCommandBuffer handles for the single submit.
    std::vector<VkCommandBuffer> rawCommandBuffers;
    rawCommandBuffers.reserve(batch.size());
    for (const auto &entry : batch)
        rawCommandBuffers.push_back(entry.commandBuffer->vk());

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
    submitInfo.commandBufferCount = static_cast<uint32_t>(rawCommandBuffers.size());
    submitInfo.pCommandBuffers    = rawCommandBuffers.data();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &completionSemaphore;

    if (vkQueueSubmit(queue, 1, &submitInfo, fence) != VK_SUCCESS)
    {
        vkDestroySemaphore(device, completionSemaphore, nullptr);
        vkDestroyFence(device, fence, nullptr);
        return false;
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

    // Store a single PendingUpload that represents the whole batch.
    // We reuse the first command buffer slot for lifetime tracking only.
    {
        std::lock_guard<std::mutex> lock(g_pendingUploadsMutex);
        g_pendingUploads.push_back(PendingUpload{
            .fence = fence,
            .completionSemaphore = completionSemaphore,
            .commandBuffer = allCommandBuffers.empty() ? nullptr : allCommandBuffers.front(),
            .stagingBuffers = std::move(allStagingBuffers),
        });
    }

    return true;
}

void AsyncGpuUpload::collectFinished(VkDevice device)
{
    std::lock_guard<std::mutex> lock(g_pendingUploadsMutex);

    size_t writeIndex = 0;
    for (size_t readIndex = 0; readIndex < g_pendingUploads.size(); ++readIndex)
    {
        auto &pendingUpload = g_pendingUploads[readIndex];

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
            g_readySemaphores.push_back(pendingUpload.completionSemaphore);
        }
        else
        {
            VX_ENGINE_ERROR_STREAM("Failed to poll upload fence status: "
                                   << core::helpers::vulkanResultToString(status) << '\n');
            if (pendingUpload.completionSemaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(device, pendingUpload.completionSemaphore, nullptr);
        }
    }

    g_pendingUploads.resize(writeIndex);
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
    {
        std::lock_guard<std::mutex> lock(g_pendingUploadsMutex);
        pendingUploads.swap(g_pendingUploads);
        readySemaphores.swap(g_readySemaphores);
    }

    for (auto &pendingUpload : pendingUploads)
    {
        if (pendingUpload.fence == VK_NULL_HANDLE)
            continue;

        if (vkWaitForFences(device, 1, &pendingUpload.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
            VX_ENGINE_ERROR_STREAM("Failed to wait for upload fence during flush\n");

        vkDestroyFence(device, pendingUpload.fence, nullptr);
        if (pendingUpload.completionSemaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(device, pendingUpload.completionSemaphore, nullptr);
    }

    releaseSemaphores(device, readySemaphores);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
