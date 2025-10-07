#include "Core/CommandPool.hpp"
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

CommandPool::CommandPool(VkDevice device, uint32_t queueFamiliIndices, VkCommandPoolCreateFlags flags) : m_device(device)
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = flags;
    poolInfo.queueFamilyIndex = queueFamiliIndices;

    if(vkCreateCommandPool(device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create command pool");
}

CommandPool::SharedPtr CommandPool::create(VkDevice device, uint32_t queueFamiliIndices, VkCommandPoolCreateFlags flags)
{
    return std::make_shared<CommandPool>(device, queueFamiliIndices, flags);
}

VkCommandPool CommandPool::vk()
{
    return m_commandPool;
}

CommandPool::~CommandPool()
{
    if(m_commandPool)
    {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
}

ELIX_NESTED_NAMESPACE_END