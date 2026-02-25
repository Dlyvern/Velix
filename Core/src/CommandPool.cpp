#include "Core/CommandPool.hpp"
#include "Core/VulkanAssert.hpp"

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

    VX_VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_handle));

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
    VX_VK_TRY(vkResetCommandPool(m_device, m_handle, flags));
}

CommandPool::~CommandPool()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END
