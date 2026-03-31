#ifndef ELIX_COMMAND_BUFFER_HPP
#define ELIX_COMMAND_BUFFER_HPP

#include "Core/Macros.hpp"
#include "Core/CommandPool.hpp"

#include <memory>
#include <vector>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class CommandBuffer
{
    DECLARE_VK_HANDLE_METHODS(VkCommandBuffer)
    DECLARE_VK_SMART_PTRS(CommandBuffer, VkCommandBuffer)
    ELIX_DECLARE_VK_LIFECYCLE()
public:
    struct SemaphoreSubmitDesc
    {
        VkSemaphore semaphore{VK_NULL_HANDLE};
        VkPipelineStageFlags2 stageMask{VK_PIPELINE_STAGE_2_NONE};
        uint64_t value{0u};
        uint32_t deviceIndex{0u};
    };

    CommandBuffer(CommandPool &commandPool, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    ~CommandBuffer();

    void createVk(CommandPool &commandPool, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    bool begin(VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, VkCommandBufferInheritanceInfo* inheritance = nullptr);
    bool end();
    void reset(VkCommandBufferResetFlags flags = 0);
    bool submit(VkQueue queue, const std::vector<VkSemaphore>& waitSemaphores = {}, const std::vector<VkPipelineStageFlags>& waitStages = {},
                const std::vector<VkSemaphore>& signalSemaphores = {}, VkFence fence = VK_NULL_HANDLE);
    bool submit2(VkQueue queue,
                 const std::vector<SemaphoreSubmitDesc> &waitSemaphores = {},
                 const std::vector<SemaphoreSubmitDesc> &signalSemaphores = {},
                 VkFence fence = VK_NULL_HANDLE,
                 uint32_t deviceMask = 0u);
private:
    CommandPool *m_commandPool{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_COMMAND_BUFFER_HPP
