#include "Engine/Material.hpp"
#include <glm/vec4.hpp>

#include <cstring>
#include <stdexcept>
#include <array>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct MaterialColor
{
    glm::vec4 color = glm::vec4(1.0f);
};

Material::Material(VkDevice device, VkDescriptorPool descriptorPool, uint32_t maxFramesInFlight, 
engine::TextureImage::SharedPtr texture, core::DescriptorSetLayout::SharedPtr descriptorSetLayout) :
m_maxFramesInFlight(maxFramesInFlight), m_texture(texture), m_device(device)
{
    std::vector<VkDescriptorSetLayout> layouts(m_maxFramesInFlight, descriptorSetLayout ? descriptorSetLayout->vk() : m_defaultDescriptorSetLayout->vk());

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(m_maxFramesInFlight);

    if(vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate decsriptor sets");

    updateDescriptorSets();
}

void Material::createDefaultMaterial(VkDevice device, VkDescriptorPool descriptorPool, uint32_t maxFramesInFlight, 
engine::TextureImage::SharedPtr texture, core::DescriptorSetLayout::SharedPtr descriptorSetLayout)
{
    s_defaultMaterial = create(device, descriptorPool, maxFramesInFlight, texture, descriptorSetLayout);
}

void Material::createDefaultDescriptorSetLayout(VkDevice device)
{
    VkDescriptorSetLayoutBinding textureLayoutBinding{};
    textureLayoutBinding.binding = 1;
    textureLayoutBinding.descriptorCount = 1;
    textureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureLayoutBinding.pImmutableSamplers = nullptr;
    textureLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding colorLayoutBinding{};
    colorLayoutBinding.binding = 2;
    colorLayoutBinding.descriptorCount = 1;
    colorLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    colorLayoutBinding.pImmutableSamplers = nullptr;
    colorLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_defaultDescriptorSetLayout = core::DescriptorSetLayout::create(device, {textureLayoutBinding, colorLayoutBinding});
}

Material::SharedPtr Material::create(VkDevice device, VkDescriptorPool descriptorPool, uint32_t maxFramesInFlight, 
engine::TextureImage::SharedPtr texture, core::DescriptorSetLayout::SharedPtr descriptorSetLayout)
{
    return std::make_shared<Material>(device, descriptorPool, maxFramesInFlight, texture, descriptorSetLayout);
}

VkDescriptorSet Material::getDescriptorSet(uint32_t frameIndex) const
{
    return m_descriptorSets[frameIndex];
}

void Material::updateDescriptorSets()
{
    m_colorBuffers.reserve(m_maxFramesInFlight);

    for(uint32_t i = 0; i < m_maxFramesInFlight; ++i)
    {
        auto colorBuffer = m_colorBuffers.emplace_back(core::Buffer::create(sizeof(MaterialColor), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

        VkDescriptorImageInfo imageInfo{};
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_colorBuffers[i]->vkBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(MaterialColor);

        void* data;
        vkMapMemory(m_device, m_colorBuffers[i]->vkDeviceMemory(), 0, sizeof(MaterialColor), 0, &data);
        std::memcpy(data, &m_color, sizeof(MaterialColor));
        vkUnmapMemory(m_device, m_colorBuffers[i]->vkDeviceMemory());

        if(m_texture)
        {
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = m_texture->vkImageView();
            imageInfo.sampler = m_texture->vkSampler();
        }

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_descriptorSets[i];
        writes[0].dstBinding = 2;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &bufferInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_descriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = m_texture ? &imageInfo : VK_NULL_HANDLE;

        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}


ELIX_NESTED_NAMESPACE_END