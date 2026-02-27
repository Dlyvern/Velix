#include "Engine/Utilities/AsyncGpuUpload.hpp"

#include "Core/Logger.hpp"
#include "Core/VulkanContext.hpp"

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

    std::mutex g_pendingUploadsMutex;
    std::vector<PendingUpload> g_pendingUploads;
    std::vector<VkSemaphore> g_readySemaphores;
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
            VX_ENGINE_ERROR_STREAM("Failed to poll upload fence status\n");
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
