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
    static bool submit(core::CommandBuffer::SharedPtr commandBuffer, VkQueue queue,
                       std::vector<core::Buffer::SharedPtr> stagingBuffers = {});

    static void collectFinished(VkDevice device);
    static std::vector<VkSemaphore> acquireReadySemaphores();
    static void releaseSemaphores(VkDevice device, const std::vector<VkSemaphore> &semaphores);
    static void flush(VkDevice device);
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASYNC_GPU_UPLOAD_HPP
