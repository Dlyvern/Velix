#ifndef ELIX_COMMAND_POOL_HPP
#define ELIX_COMMAND_POOL_HPP

#include <memory>
#include "Core/Macros.hpp"

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class CommandPool
{
    DECLARE_VK_HANDLE_METHODS(VkCommandPool)
    DECLARE_VK_SMART_PTRS(CommandPool, VkCommandPool)
    ELIX_DECLARE_VK_LIFECYCLE()

public:
    CommandPool(VkDevice device, uint32_t queueFamiliIndices, VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    void createVk(uint32_t queueFamiliIndices, VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    ~CommandPool();
    void reset(VkCommandPoolResetFlags flags = 0);

private:
    VkDevice m_device{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_COMMAND_POOL_HPP