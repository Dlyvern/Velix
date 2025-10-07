#ifndef ELIX_DESCRIPTOR_SET_BUILDER_HPP
#define ELIX_DESCRIPTOR_SET_BUILDER_HPP

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"

#include <vector>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class DescriptorSetBuilder
{
public:
    static DescriptorSetBuilder begin();
    DescriptorSetBuilder& addImage();
    DescriptorSetBuilder& addBuffer(core::Buffer::SharedPtr buffer, VkDeviceSize range, uint32_t binding, VkDescriptorSet descriptorSet);

    void build(VkDevice device);

private:
    struct WriterData
    {
        VkDescriptorBufferInfo buffer{VK_NULL_HANDLE};
        VkDescriptorImageInfo image{VK_NULL_HANDLE};
    };

    std::vector<std::pair<VkWriteDescriptorSet, WriterData>> m_writers;
    // std::vector<VkWriteDescriptorSet> m_writers;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_DESCRIPTOR_SET_BUILDER_HPP