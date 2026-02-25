#include "Core/DescriptorPool.hpp"
#include "Core/VulkanAssert.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)

DescriptorPool::DescriptorPool(VkDevice device, const std::vector<VkDescriptorPoolSize> &poolSize, VkDescriptorPoolCreateFlags flags, uint32_t maxSets) : m_device(device)
{
    createVk(poolSize, flags, maxSets);
}

void DescriptorPool::createVk(const std::vector<VkDescriptorPoolSize> &poolSize, VkDescriptorPoolCreateFlags flags, uint32_t maxSets)
{
    ELIX_VK_CREATE_GUARD()

    VkDescriptorPoolCreateInfo descriptorPoolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptorPoolCI.flags = flags;
    descriptorPoolCI.maxSets = maxSets;
    descriptorPoolCI.poolSizeCount = std::size(poolSize);
    descriptorPoolCI.pPoolSizes = poolSize.data();

    VX_VK_CHECK(vkCreateDescriptorPool(m_device, &descriptorPoolCI, nullptr, &m_handle));

    ELIX_VK_CREATE_GUARD_DONE()
}

void DescriptorPool::destroyVkImpl()
{
    if (m_handle)
    {
        vkDestroyDescriptorPool(m_device, m_handle, nullptr);
        m_handle = VK_NULL_HANDLE;
    }
}

DescriptorPool::~DescriptorPool()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END
