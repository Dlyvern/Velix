#include "Core/DescriptorSetLayout.hpp"
#include "Core/VulkanContext.hpp"
#include <cstdint>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

DescriptorSetLayout::DescriptorSetLayout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding> &bindings) : m_device(device)
{
    createVk(bindings);
}

void DescriptorSetLayout::createVk(const std::vector<VkDescriptorSetLayoutBinding> &bindings)
{
    ELIX_VK_CREATE_GUARD()

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_handle) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor set layout");

    ELIX_VK_CREATE_GUARD_DONE()
}

void DescriptorSetLayout::destroyVkImpl()
{
    if (m_handle)
    {
        vkDestroyDescriptorSetLayout(VulkanContext::getContext()->getDevice(), m_handle, nullptr);
        m_handle = VK_NULL_HANDLE;
    }
}

DescriptorSetLayout::~DescriptorSetLayout()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END