#ifndef ELIX_COMMAND_BUFFER_HPP
#define ELIX_COMMAND_BUFFER_HPP

#include <memory>
#include <vector>

#include "Macros.hpp"

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class CommandBuffer
{
public:
    using SharedPtr = std::shared_ptr<CommandBuffer>;

    CommandBuffer(VkDevice device, VkCommandPool commandPool, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    static SharedPtr create(VkDevice device, VkCommandPool commandPool, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    ~CommandBuffer();
    bool begin(VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, VkCommandBufferInheritanceInfo* inheritance = nullptr);
    bool end();
    void reset(VkCommandBufferResetFlags flags = 0);
    void submit(VkQueue queue, const std::vector<VkSemaphore>& waitSemaphores, const std::vector<VkPipelineStageFlags>& waitStages,
    const std::vector<VkSemaphore>& signalSemaphores, VkFence fence);

    VkCommandBuffer vk();
    VkCommandBuffer* pVk();

private:
    VkCommandBuffer m_commandBuffer{VK_NULL_HANDLE};
    VkCommandPool m_commandPool{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
};


ELIX_NESTED_NAMESPACE_END

#endif //ELIX_COMMAND_BUFFER_HPP