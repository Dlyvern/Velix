#ifndef ELIX_ASYNC_GPU_UPLOAD_HPP
#define ELIX_ASYNC_GPU_UPLOAD_HPP

#include "Core/Macros.hpp"

#include "Core/Buffer.hpp"
#include "Core/CommandBuffer.hpp"

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(utilities)

class AsyncGpuUpload
{
public:
    // Submit a single command buffer immediately (one vkQueueSubmit per call).
    // Use only for infrequent one-off uploads (skybox, IBL, etc.).
    static bool submit(core::CommandBuffer::SharedPtr commandBuffer, VkQueue queue,
                       std::vector<core::Buffer::SharedPtr> stagingBuffers = {});

    // Enqueue a command buffer to be submitted in the next batchFlush() call.
    // Use this for texture uploads during frame preparation to avoid per-upload vkQueueSubmit overhead.
    static void enqueue(core::CommandBuffer::SharedPtr commandBuffer,
                        std::vector<core::Buffer::SharedPtr> stagingBuffers = {});

    // Submit all enqueued command buffers in a single vkQueueSubmit call.
    // Returns false if there was nothing to submit or if submission failed.
    static bool batchFlush(VkQueue queue);

    static void collectFinished(VkDevice device);
    static std::vector<VkSemaphore> acquireReadySemaphores();
    static void releaseSemaphores(VkDevice device, const std::vector<VkSemaphore> &semaphores);
    static void flush(VkDevice device);
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASYNC_GPU_UPLOAD_HPP
