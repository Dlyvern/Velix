#include "Core/CommandPool.hpp"
#include "Core/VulkanHelpers.hpp"
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

CommandPool::CommandPool(VkDevice device, uint32_t queueFamiliIndices, VkCommandPoolCreateFlags flags) : m_device(device)
{
    createVk(queueFamiliIndices, flags);
}

void CommandPool::createVk(uint32_t queueFamiliIndices, VkCommandPoolCreateFlags flags)
{
    ELIX_VK_CREATE_GUARD()

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = flags;
    poolInfo.queueFamilyIndex = queueFamiliIndices;

    if (VkResult result = vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_handle); result != VK_SUCCESS)
        throw std::runtime_error("Failed to create command pool " + helpers::vulkanResultToString(result));

    ELIX_VK_CREATE_GUARD_DONE()
}

void CommandPool::destroyVkImpl()
{
    if (m_handle)
    {
        vkDestroyCommandPool(m_device, m_handle, nullptr);
        m_handle = VK_NULL_HANDLE;
    }
}

void CommandPool::reset(VkCommandPoolResetFlags flags)
{
    vkResetCommandPool(m_device, m_handle, flags);
}

CommandPool::~CommandPool()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END