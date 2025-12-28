#include "Engine/Material.hpp"
#include <glm/vec4.hpp>

#include <cstring>
#include <stdexcept>
#include <array>

#include "Core/VulkanContext.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/RenderGraph.hpp"
#include "Engine/ShaderFamily.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct MaterialColor
{
    glm::vec4 color = glm::vec4(1.0f);
};

Material::Material(VkDescriptorPool descriptorPool, engine::TextureImage::SharedPtr texture) : m_texture(texture)
{
    m_device = core::VulkanContext::getContext()->getDevice();

    m_maxFramesInFlight = RenderGraph::MAX_FRAMES_IN_FLIGHT;

    m_descriptorSets.resize(m_maxFramesInFlight);
    m_colorBuffers.reserve(m_maxFramesInFlight);

    for(uint32_t i = 0; i < m_maxFramesInFlight; ++i)
    {
        auto colorBuffer = m_colorBuffers.emplace_back(core::Buffer::createShared(sizeof(MaterialColor), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
        core::memory::MemoryUsage::CPU_TO_GPU));

        colorBuffer->upload(&m_color, sizeof(MaterialColor));

        m_descriptorSets[i] = DescriptorSetBuilder::begin()
        .addBuffer(colorBuffer, sizeof(MaterialColor), 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .addImage(m_texture->vkImageView(), m_texture->vkSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
        .build(m_device, descriptorPool, engineShaderFamilies::staticMeshMaterialLayout->vk());
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

void Material::createDefaultMaterial(VkDescriptorPool descriptorPool, engine::TextureImage::SharedPtr texture)
{
    s_defaultMaterial = create(descriptorPool, texture);
}

Material::SharedPtr Material::create(VkDescriptorPool descriptorPool, engine::TextureImage::SharedPtr texture)
{
    return std::make_shared<Material>(descriptorPool, texture);
}

VkDescriptorSet Material::getDescriptorSet(uint32_t frameIndex) const
{
    return m_descriptorSets[frameIndex];
}

void Material::deleteDefaultMaterial()
{
    s_defaultMaterial.reset();
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