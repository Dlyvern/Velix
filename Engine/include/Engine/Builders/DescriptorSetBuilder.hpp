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
    DescriptorSetBuilder& addImage(VkImageView imageView, VkSampler sampler, VkImageLayout imageLayout, uint32_t binding);
    DescriptorSetBuilder& addBuffer(core::Buffer::SharedPtr buffer, VkDeviceSize range, uint32_t binding, VkDescriptorType descriptorType);

    VkDescriptorSet build(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorSetLayout layout);

    void update(VkDevice device, VkDescriptorSet dst);

private:
    struct BufferInfo
    {
        core::Buffer::SharedPtr buffer{nullptr};
        uint32_t binding{0};
        VkDeviceSize range{0};
        VkDescriptorType descriptorType{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
    };

    struct ImageInfo
    {
        VkImageView imageView{VK_NULL_HANDLE};
        VkSampler sampler{VK_NULL_HANDLE};
        VkImageLayout imageLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        uint32_t binding{0};
    };

    std::vector<BufferInfo> m_bufferInfos;
    std::vector<ImageInfo> m_imageInfos;

    std::vector<VkWriteDescriptorSet> m_writers;

    void createWriters(VkDescriptorSet dstSet, std::vector<VkWriteDescriptorSet>& writers, std::vector<VkDescriptorBufferInfo>& bufferInfos, std::vector<VkDescriptorImageInfo>& imageInfos);
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_DESCRIPTOR_SET_BUILDER_HPP