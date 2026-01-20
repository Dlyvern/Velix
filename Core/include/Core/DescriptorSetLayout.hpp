#ifndef ELIX_DESCRIPTOR_SET_LAYOUT_HPP
#define ELIX_DESCRIPTOR_SET_LAYOUT_HPP

#include "Core/Macros.hpp"

#include <vector>
#include <memory>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class DescriptorSetLayout
{
    DECLARE_VK_HANDLE_METHODS(VkDescriptorSetLayout)
    ELIX_DECLARE_VK_LIFECYCLE()
    DECLARE_VK_SMART_PTRS(DescriptorSetLayout, VkDescriptorSetLayout)

public:
    DescriptorSetLayout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding> &bindings);
    void createVk(const std::vector<VkDescriptorSetLayoutBinding> &bindings);
    ~DescriptorSetLayout();

private:
    VkDevice m_device{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_DESCRIPTOR_SET_LAYOUT_HPP