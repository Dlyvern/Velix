#include "Engine/Builders/DescriptorSetBuilder.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

DescriptorSetBuilder DescriptorSetBuilder::begin()
{
    return DescriptorSetBuilder{};
}

DescriptorSetBuilder& DescriptorSetBuilder::addBuffer(core::Buffer::SharedPtr buffer, VkDeviceSize range, uint32_t binding, VkDescriptorSet descriptorSet)
{
    WriterData data;

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer->vkBuffer();
    bufferInfo.offset = 0;
    bufferInfo.range = range;

    VkWriteDescriptorSet writer{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writer.dstSet = descriptorSet;
    writer.dstBinding = binding;
    writer.dstArrayElement = 0;
    writer.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writer.descriptorCount = 1;
    writer.pBufferInfo = &bufferInfo;

    data.buffer = bufferInfo;

    m_writers.push_back({writer, data});

    return *this;
}

DescriptorSetBuilder& DescriptorSetBuilder::addImage()
{
    return *this;
}

void DescriptorSetBuilder::build(VkDevice device)
{
    std::vector<VkWriteDescriptorSet> writers(m_writers.size());

    for(const auto& writer  : m_writers)
        writers.push_back(writer.first);

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writers.size()), writers.data(), 0, nullptr);
}

ELIX_NESTED_NAMESPACE_END