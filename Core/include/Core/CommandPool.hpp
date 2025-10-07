#ifndef ELIX_COMMAND_POOL_HPP
#define ELIX_COMMAND_POOL_HPP

#include <memory>
#include "Macros.hpp"

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class CommandPool
{
public:
    using SharedPtr = std::shared_ptr<CommandPool>;

    CommandPool(VkDevice device, uint32_t queueFamiliIndices, VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    static SharedPtr create(VkDevice device, uint32_t queueFamiliIndices, VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    VkCommandPool vk();

    ~CommandPool();
private:
    VkCommandPool m_commandPool{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
};


ELIX_NESTED_NAMESPACE_END

#endif //ELIX_COMMAND_POOL_HPP