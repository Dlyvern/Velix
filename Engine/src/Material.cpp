#include "Engine/Material.hpp"
#include <glm/vec4.hpp>

#include <cstring>
#include <stdexcept>
#include <array>

#include "Engine/Builders/DescriptorSetBuilder.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct MaterialColor
{
    glm::vec4 color = glm::vec4(1.0f);
};

Material::Material(VkDevice device, VkPhysicalDevice physicalDevice, VkDescriptorPool descriptorPool, uint32_t maxFramesInFlight, 
engine::TextureImage::SharedPtr texture, core::DescriptorSetLayout::SharedPtr descriptorSetLayout) :
m_maxFramesInFlight(maxFramesInFlight), m_texture(texture), m_device(device), m_descriptorPool(descriptorPool), m_descriptorSetLayout(descriptorSetLayout)
{
    m_descriptorSets.resize(m_maxFramesInFlight);
    m_colorBuffers.reserve(m_maxFramesInFlight);

    for(uint32_t i = 0; i < m_maxFramesInFlight; ++i)
    {
        auto colorBuffer = m_colorBuffers.emplace_back(core::Buffer::create(device, physicalDevice, sizeof(MaterialColor), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

        colorBuffer->upload(&m_color, sizeof(MaterialColor));

        m_descriptorSets[i] = DescriptorSetBuilder::begin()
        .addBuffer(colorBuffer, sizeof(MaterialColor), 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .addImage(m_texture->vkImageView(), m_texture->vkSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
        .build(m_device, m_descriptorPool, m_descriptorSetLayout->vk());
    }
}

void Material::setTexture(TextureImage::SharedPtr texture)
{
    m_texture = texture;
    updateDescriptorSets();
}

void Material::setColor(const glm::vec4& color)
{
    m_color = color;
    updateDescriptorSets();
}

const glm::vec4& Material::getColor() const
{
    return m_color;
}

TextureImage::SharedPtr Material::getTexture() const
{
    return m_texture;
}

void Material::createDefaultMaterial(VkDevice device, VkPhysicalDevice physicalDevice, VkDescriptorPool descriptorPool, uint32_t maxFramesInFlight, 
engine::TextureImage::SharedPtr texture, core::DescriptorSetLayout::SharedPtr descriptorSetLayout)
{
    s_defaultMaterial = create(device, physicalDevice,descriptorPool, maxFramesInFlight, texture, descriptorSetLayout);
}

Material::SharedPtr Material::create(VkDevice device, VkPhysicalDevice physicalDevice, VkDescriptorPool descriptorPool, uint32_t maxFramesInFlight, 
engine::TextureImage::SharedPtr texture, core::DescriptorSetLayout::SharedPtr descriptorSetLayout)
{
    return std::make_shared<Material>(device, physicalDevice, descriptorPool, maxFramesInFlight, texture, descriptorSetLayout);
}

VkDescriptorSet Material::getDescriptorSet(uint32_t frameIndex) const
{
    return m_descriptorSets[frameIndex];
}

void Material::updateDescriptorSets()
{
    for(uint32_t i = 0; i < m_maxFramesInFlight; ++i)
    {
        m_colorBuffers[i]->upload(&m_color, sizeof(MaterialColor));
        
        DescriptorSetBuilder::begin()
        .addBuffer(m_colorBuffers[i], sizeof(MaterialColor), 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .addImage(m_texture->vkImageView(), m_texture->vkSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
        .update(m_device, m_descriptorSets[i]);
    }
}

ELIX_NESTED_NAMESPACE_END