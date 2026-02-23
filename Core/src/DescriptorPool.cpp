#include "Core/DescriptorPool.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)

DescriptorPool::DescriptorPool(VkDevice device, const std::vector<VkDescriptorPoolSize> &poolSize, VkDescriptorPoolCreateFlags flags, uint32_t maxSets) : m_device(device)
{
    createVk(poolSize, flags, maxSets);
}

void DescriptorPool::createVk(const std::vector<VkDescriptorPoolSize> &poolSize, VkDescriptorPoolCreateFlags flags, uint32_t maxSets)
{
    VkDescriptorPoolCreateInfo descriptorPoolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptorPoolCI.flags = flags;
    descriptorPoolCI.maxSets = maxSets;
    descriptorPoolCI.poolSizeCount = std::size(poolSize);
    descriptorPoolCI.pPoolSizes = poolSize.data();

    if (vkCreateDescriptorPool(m_device, &descriptorPoolCI, nullptr, &m_handle) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor pool");
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