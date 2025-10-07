#ifndef ELIX_DESCRIPTOR_SET_LAYOUT_HPP
#define ELIX_DESCRIPTOR_SET_LAYOUT_HPP

#include "Core/Macros.hpp"

#include <vector>
#include <memory>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class DescriptorSetLayout
{
public:
    using SharedPtr = std::shared_ptr<DescriptorSetLayout>;

    DescriptorSetLayout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding>& bindings);
    ~DescriptorSetLayout();

    static SharedPtr create(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding>& bindings);

    VkDescriptorSetLayout vk();
private:
    VkDevice m_device{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_DESCRIPTOR_SET_LAYOUT_HPP