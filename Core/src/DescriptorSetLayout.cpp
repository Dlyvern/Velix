#include "Core/DescriptorSetLayout.hpp"
#include <cstdint>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

DescriptorSetLayout::DescriptorSetLayout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding>& bindings) : m_device(device)
{
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor set layout");
}

VkDescriptorSetLayout DescriptorSetLayout::vk()
{
    return m_descriptorSetLayout;
}

DescriptorSetLayout::SharedPtr DescriptorSetLayout::create(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding>& bindings)
{
    return std::make_shared<DescriptorSetLayout>(device, bindings);
}

DescriptorSetLayout::~DescriptorSetLayout()
{
    if(m_descriptorSetLayout)
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
}

ELIX_NESTED_NAMESPACE_END