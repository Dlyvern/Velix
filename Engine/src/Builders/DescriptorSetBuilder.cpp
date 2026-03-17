#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Core/Logger.hpp"

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

DescriptorSetBuilder& DescriptorSetBuilder::addStorageImage(VkImageView imageView, VkImageLayout imageLayout, uint32_t binding)
{
    ImageInfo imageInfo{};
    imageInfo.imageLayout = imageLayout;
    imageInfo.imageView = imageView;
    imageInfo.binding = binding;
    imageInfo.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    m_imageInfos.push_back(std::move(imageInfo));

    return *this;
}

DescriptorSetBuilder& DescriptorSetBuilder::addAccelerationStructure(VkAccelerationStructureKHR accelerationStructure, uint32_t binding)
{
    AccelerationStructureInfo accelerationStructureInfo{};
    accelerationStructureInfo.accelerationStructure = accelerationStructure;
    accelerationStructureInfo.binding = binding;

    m_accelerationStructureInfos.push_back(std::move(accelerationStructureInfo));

    return *this;
}

void DescriptorSetBuilder::update(VkDevice device, VkDescriptorSet dst)
{
    if (device == VK_NULL_HANDLE)
    {
        VX_ENGINE_ERROR_STREAM("DescriptorSetBuilder::update skipped: device is VK_NULL_HANDLE\n");
        return;
    }

    if (dst == VK_NULL_HANDLE)
    {
        VX_ENGINE_ERROR_STREAM("DescriptorSetBuilder::update skipped: destination descriptor set is VK_NULL_HANDLE "
                               << "(images=" << m_imageInfos.size()
                               << ", buffers=" << m_bufferInfos.size()
                               << ", accelerationStructures=" << m_accelerationStructureInfos.size() << ")\n");
        return;
    }

    std::vector<VkWriteDescriptorSet> writers;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> accelerationStructureInfos;

    writers.reserve(m_bufferInfos.size() + m_imageInfos.size() + m_accelerationStructureInfos.size());
    bufferInfos.reserve(m_bufferInfos.size());
    imageInfos.reserve(m_imageInfos.size());
    accelerationStructureInfos.reserve(m_accelerationStructureInfos.size());

    createWriters(dst, writers, bufferInfos, imageInfos, accelerationStructureInfos);

    if (!writers.empty())
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writers.size()), writers.data(), 0, nullptr);
}

void DescriptorSetBuilder::createWriters(VkDescriptorSet dstSet,
                                         std::vector<VkWriteDescriptorSet>& writers,
                                         std::vector<VkDescriptorBufferInfo>& bufferInfos,
                                         std::vector<VkDescriptorImageInfo>& imageInfos,
                                         std::vector<VkWriteDescriptorSetAccelerationStructureKHR>& accelerationStructureInfos)
{
    for (const auto& buffer : m_bufferInfos)
    {
        VkDescriptorBufferInfo& descriptorBufferInfo = bufferInfos.emplace_back();
        descriptorBufferInfo.buffer = buffer.buffer->vk();
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

    for (const auto& image : m_imageInfos)
    {
        VkDescriptorImageInfo& descriptorImageInfo = imageInfos.emplace_back();
        descriptorImageInfo.imageLayout = image.imageLayout;
        descriptorImageInfo.imageView = image.imageView;
        descriptorImageInfo.sampler = image.sampler;

        VkWriteDescriptorSet& writer = writers.emplace_back();
        writer.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writer.descriptorType = image.descriptorType;
        writer.descriptorCount = 1;
        writer.dstArrayElement = 0;
        writer.pImageInfo = &descriptorImageInfo;
        writer.dstSet = dstSet;
        writer.dstBinding = image.binding;
    }

    for (const auto& accelerationStructure : m_accelerationStructureInfos)
    {
        auto& descriptorAccelerationStructureInfo = accelerationStructureInfos.emplace_back();
        descriptorAccelerationStructureInfo.sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
        descriptorAccelerationStructureInfo.pAccelerationStructures = &accelerationStructure.accelerationStructure;

        VkWriteDescriptorSet& writer = writers.emplace_back();
        writer.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writer.pNext = &descriptorAccelerationStructureInfo;
        writer.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        writer.descriptorCount = 1;
        writer.dstArrayElement = 0;
        writer.pImageInfo = nullptr;
        writer.pBufferInfo = nullptr;
        writer.pTexelBufferView = nullptr;
        writer.dstSet = dstSet;
        writer.dstBinding = accelerationStructure.binding;
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

    if (VkResult result = vkAllocateDescriptorSets(device, &allocateInfo, &descriptorSet); result != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate descriptor set " + core::helpers::vulkanResultToString(result));

    update(device, descriptorSet);

    return descriptorSet;
}

ELIX_NESTED_NAMESPACE_END
