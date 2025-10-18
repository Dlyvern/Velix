#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

DescriptorSetBuilder DescriptorSetBuilder::begin()
{
    return DescriptorSetBuilder{};
}

DescriptorSetBuilder& DescriptorSetBuilder::addBuffer(core::Buffer::SharedPtr buffer, VkDeviceSize range, uint32_t binding, VkDescriptorType descriptorType)
{
    BufferInfo bufferInfo{};
    bufferInfo.binding = binding;
    bufferInfo.range = range;
    bufferInfo.descriptorType = descriptorType;
    bufferInfo.buffer = buffer;

    m_bufferInfos.push_back(std::move(bufferInfo));

    return *this;
}

DescriptorSetBuilder& DescriptorSetBuilder::addImage(VkImageView imageView, VkSampler sampler, VkImageLayout imageLayout, uint32_t binding)
{
    ImageInfo imageInfo{};
    imageInfo.imageLayout = imageLayout;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;
    imageInfo.binding = binding;

    m_imageInfos.push_back(std::move(imageInfo));

    return *this;
}

void DescriptorSetBuilder::update(VkDevice device, VkDescriptorSet dst)
{
    std::vector<VkWriteDescriptorSet> writers;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;

    createWriters(dst, writers, bufferInfos, imageInfos);

    if(!writers.empty())
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writers.size()), writers.data(), 0, nullptr);
}

void DescriptorSetBuilder::createWriters(VkDescriptorSet dstSet, std::vector<VkWriteDescriptorSet>& writers, std::vector<VkDescriptorBufferInfo>& bufferInfos, std::vector<VkDescriptorImageInfo>& imageInfos)
{
    for(const auto& buffer : m_bufferInfos)
    {
        VkDescriptorBufferInfo& descriptorBufferInfo = bufferInfos.emplace_back();
        descriptorBufferInfo.buffer = buffer.buffer->vkBuffer();
        descriptorBufferInfo.offset = 0;
        descriptorBufferInfo.range = buffer.range;

        VkWriteDescriptorSet& writer = writers.emplace_back();
        writer.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writer.descriptorType = buffer.descriptorType;
        writer.descriptorCount = 1;
        writer.pBufferInfo = &descriptorBufferInfo;
        writer.dstBinding = buffer.binding;
        writer.dstArrayElement = 0;
        writer.dstSet = dstSet;
    }

    for(const auto& image : m_imageInfos)
    {
        VkDescriptorImageInfo& descriptorImageInfo = imageInfos.emplace_back();
        descriptorImageInfo.imageLayout = image.imageLayout;
        descriptorImageInfo.imageView = image.imageView;
        descriptorImageInfo.sampler = image.sampler;

        VkWriteDescriptorSet& writer = writers.emplace_back();
        writer.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writer.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writer.descriptorCount = 1;
        writer.dstArrayElement = 0;
        writer.pImageInfo = &descriptorImageInfo;
        writer.dstSet = dstSet;
        writer.dstBinding = image.binding;
    }
}

VkDescriptorSet DescriptorSetBuilder::build(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorSetLayout layout)
{
    VkDescriptorSet descriptorSet{};

    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = descriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &layout;
    allocateInfo.pNext = nullptr;

    if(vkAllocateDescriptorSets(device, &allocateInfo, &descriptorSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate descriptor set");

    update(device, descriptorSet);

    return descriptorSet;
}

ELIX_NESTED_NAMESPACE_END