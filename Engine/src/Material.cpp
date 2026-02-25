#include "Engine/Material.hpp"
#include <glm/vec4.hpp>

#include "Core/VulkanContext.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Render/RenderGraph/RenderGraph.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

namespace
{
    float clamp01(float v)
    {
        return std::max(0.0f, std::min(1.0f, v));
    }

    // TODO: replace with your actual texture defaults storage
    elix::engine::Texture::SharedPtr getDefaultWhiteTexture()
    {
        return elix::engine::Texture::getDefaultWhiteTexture(); // implement this in your engine
    }

    elix::engine::Texture::SharedPtr getDefaultNormalTexture()
    {
        return elix::engine::Texture::getDefaultNormalTexture(); // flat normal = (0.5, 0.5, 1.0)
    }

    elix::engine::Texture::SharedPtr getDefaultOrmTexture()
    {
        return elix::engine::Texture::getDefaultOrmTexture(); // AO=1, Roughness=1, Metallic=0
    }

    elix::engine::Texture::SharedPtr getDefaultBlackTexture()
    {
        return elix::engine::Texture::getDefaultBlackTexture();
    }
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct MaterialColor
{
    glm::vec4 color = glm::vec4(1.0f);
};

Material::Material(Texture::SharedPtr albedoTexture) : m_albedoTexture(albedoTexture)
{
    m_device = core::VulkanContext::getContext()->getDevice();
    m_maxFramesInFlight = renderGraph::RenderGraph::MAX_FRAMES_IN_FLIGHT;

    // Fallbacks
    if (!m_albedoTexture)
        m_albedoTexture = getDefaultWhiteTexture();

    m_normalTexture = getDefaultNormalTexture();
    m_ormTexture = getDefaultOrmTexture();
    m_emissiveTexture = getDefaultBlackTexture();

    m_descriptorSets.resize(m_maxFramesInFlight);
    m_paramBuffers.reserve(m_maxFramesInFlight);

    for (uint32_t i = 0; i < m_maxFramesInFlight; ++i)
    {
        auto paramBuffer = core::Buffer::createShared(sizeof(GPUParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU);
        paramBuffer->upload(&m_params, sizeof(GPUParams));
        m_paramBuffers.emplace_back(paramBuffer);
    }

    createDescriptorSets();
    updateTextureDescriptors();
}

void Material::uploadParams()
{
    for (uint32_t i = 0; i < m_maxFramesInFlight; ++i)
        m_paramBuffers[i]->upload(&m_params, sizeof(GPUParams));
}

void Material::updateTextureDescriptors()
{
    if (!m_albedoTexture)
        m_albedoTexture = getDefaultWhiteTexture();
    if (!m_normalTexture)
        m_normalTexture = getDefaultNormalTexture();
    if (!m_ormTexture)
        m_ormTexture = getDefaultOrmTexture();
    if (!m_emissiveTexture)
        m_emissiveTexture = getDefaultBlackTexture();

    for (uint32_t i = 0; i < m_maxFramesInFlight; ++i)
    {
        DescriptorSetBuilder::begin()
            .addImage(m_albedoTexture->vkImageView(), m_albedoTexture->vkSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
            .addImage(m_normalTexture->vkImageView(), m_normalTexture->vkSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
            .addImage(m_ormTexture->vkImageView(), m_ormTexture->vkSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
            .addImage(m_emissiveTexture->vkImageView(), m_emissiveTexture->vkSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 3)
            .addBuffer(m_paramBuffers[i], sizeof(GPUParams), 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .update(m_device, m_descriptorSets[i]);
    }
}

void Material::createDescriptorSets()
{
    auto descriptorPool = core::VulkanContext::getContext()->getPersistentDescriptorPool();

    for (uint32_t i = 0; i < m_maxFramesInFlight; ++i)
    {
        // Layout proposal:
        // set=1, binding=0 -> albedo sampler2D
        // set=1, binding=1 -> normal sampler2D
        // set=1, binding=2 -> orm sampler2D
        // set=1, binding=3 -> emissive sampler2D
        // set=1, binding=4 -> MaterialParams UBO
        m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                  .addImage(m_albedoTexture->vkImageView(), m_albedoTexture->vkSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                  .addImage(m_normalTexture->vkImageView(), m_normalTexture->vkSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                                  .addImage(m_ormTexture->vkImageView(), m_ormTexture->vkSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                                  .addImage(m_emissiveTexture->vkImageView(), m_emissiveTexture->vkSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 3)
                                  .addBuffer(m_paramBuffers[i], sizeof(GPUParams), 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                  .build(m_device, descriptorPool, EngineShaderFamilies::materialDescriptorSetLayout->vk());
    }
}

void Material::createDefaultMaterial(Texture::SharedPtr texture)
{
    s_defaultMaterial = create(texture);
}

Material::SharedPtr Material::create(Texture::SharedPtr texture)
{
    return std::make_shared<Material>(texture);
}

VkDescriptorSet Material::getDescriptorSet(uint32_t frameIndex) const
{
    return m_descriptorSets[frameIndex];
}

void Material::deleteDefaultMaterial()
{
    s_defaultMaterial.reset();
}

void Material::setAlbedoTexture(Texture::SharedPtr texture)
{
    m_albedoTexture = texture ? texture : getDefaultWhiteTexture();
    updateTextureDescriptors();
}

void Material::setNormalTexture(Texture::SharedPtr texture)
{
    m_normalTexture = texture ? texture : getDefaultNormalTexture();
    updateTextureDescriptors();
}

void Material::setOrmTexture(Texture::SharedPtr texture)
{
    m_ormTexture = texture ? texture : getDefaultOrmTexture();
    updateTextureDescriptors();
}

void Material::setEmissiveTexture(Texture::SharedPtr texture)
{
    m_emissiveTexture = texture ? texture : getDefaultBlackTexture();
    updateTextureDescriptors();
}

Texture::SharedPtr Material::getAlbedoTexture() const { return m_albedoTexture; }
Texture::SharedPtr Material::getNormalTexture() const { return m_normalTexture; }
Texture::SharedPtr Material::getOrmTexture() const { return m_ormTexture; }
Texture::SharedPtr Material::getEmissiveTexture() const { return m_emissiveTexture; }

// ---------------- Params ----------------

void Material::setBaseColorFactor(const glm::vec4 &color)
{
    m_params.baseColorFactor = color;
    uploadParams();
}

void Material::setEmissiveFactor(const glm::vec3 &emissive)
{
    m_params.emissiveFactor = glm::vec4(emissive, 0.0f);
    uploadParams();
}

void Material::setMetallic(float metallic)
{
    m_params.metallicFactor = clamp01(metallic);
    uploadParams();
}

void Material::setRoughness(float roughness)
{
    m_params.roughnessFactor = std::max(0.04f, clamp01(roughness)); // avoid 0
    uploadParams();
}

void Material::setNormalScale(float normalScale)
{
    m_params.normalScale = std::max(0.0f, normalScale);
    uploadParams();
}

void Material::setAoStrength(float aoStrength)
{
    m_params.aoStrength = clamp01(aoStrength);
    uploadParams();
}

void Material::setUVScale(const glm::vec2 &scale)
{
    m_params.uvTransform.x = scale.x;
    m_params.uvTransform.y = scale.y;
    uploadParams();
}

void Material::setUVOffset(const glm::vec2 &offset)
{
    m_params.uvTransform.z = offset.x;
    m_params.uvTransform.w = offset.y;
    uploadParams();
}

void Material::setAlphaCutoff(float cutoff)
{
    m_params.alphaCutoff = clamp01(cutoff);
    uploadParams();
}

const Material::GPUParams &Material::params() const
{
    return m_params;
}

void Material::setFlags(uint32_t flags)
{
    m_params.flags = flags;
    uploadParams();
}

ELIX_NESTED_NAMESPACE_END