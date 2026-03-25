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

    elix::engine::Texture::SharedPtr getDefaultWhiteTexture()
    {
        return elix::engine::Texture::getDefaultWhiteTexture();
    }

    elix::engine::Texture::SharedPtr getDefaultNormalTexture()
    {
        return elix::engine::Texture::getDefaultNormalTexture();
    }

    elix::engine::Texture::SharedPtr getDefaultOrmTexture()
    {
        return elix::engine::Texture::getDefaultOrmTexture();
    }

    elix::engine::Texture::SharedPtr getDefaultBlackTexture()
    {
        return elix::engine::Texture::getDefaultBlackTexture();
    }
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Material::Material(Texture::SharedPtr albedoTexture) : m_albedoTexture(albedoTexture)
{
    m_device = core::VulkanContext::getContext()->getDevice();
    m_maxFramesInFlight = renderGraph::RenderGraph::MAX_FRAMES_IN_FLIGHT;

    if (!m_albedoTexture)
        m_albedoTexture = getDefaultWhiteTexture();

    m_normalTexture   = getDefaultNormalTexture();
    m_ormTexture      = getDefaultOrmTexture();
    m_emissiveTexture = getDefaultBlackTexture();

    m_descriptorSets.resize(m_maxFramesInFlight);
    m_paramBuffers.reserve(m_maxFramesInFlight);

    for (uint32_t i = 0; i < m_maxFramesInFlight; ++i)
    {
        auto paramBuffer = core::Buffer::createShared(sizeof(GPUParams),
                                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                      core::memory::MemoryUsage::CPU_TO_GPU);
        paramBuffer->upload(&m_params, sizeof(GPUParams));
        m_paramBuffers.emplace_back(paramBuffer);
    }

    createDescriptorSets();
    updateTextureDescriptors();
}

void Material::createDescriptorSets()
{
    auto descriptorPool = core::VulkanContext::getContext()->getPersistentDescriptorPool();

    for (uint32_t i = 0; i < m_maxFramesInFlight; ++i)
    {
        m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                  .addImage(m_albedoTexture->vkImageView(), m_albedoTexture->vkSampler(),
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                  .addImage(m_normalTexture->vkImageView(), m_normalTexture->vkSampler(),
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                                  .addImage(m_ormTexture->vkImageView(), m_ormTexture->vkSampler(),
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                                  .addImage(m_emissiveTexture->vkImageView(), m_emissiveTexture->vkSampler(),
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 3)
                                  .addBuffer(m_paramBuffers[i], sizeof(GPUParams), 4,
                                             VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                  .build(m_device, descriptorPool,
                                         EngineShaderFamilies::materialDescriptorSetLayout->vk());
    }
}

void Material::updateTextureDescriptors()
{
    if (!m_albedoTexture)  m_albedoTexture  = getDefaultWhiteTexture();
    if (!m_normalTexture)  m_normalTexture  = getDefaultNormalTexture();
    if (!m_ormTexture)     m_ormTexture     = getDefaultOrmTexture();
    if (!m_emissiveTexture) m_emissiveTexture = getDefaultBlackTexture();

    for (uint32_t i = 0; i < m_maxFramesInFlight; ++i)
    {
        if (m_descriptorSets[i] == VK_NULL_HANDLE)
        {
            auto descriptorPool = core::VulkanContext::getContext()->getPersistentDescriptorPool();
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(m_albedoTexture->vkImageView(), m_albedoTexture->vkSampler(),
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .addImage(m_normalTexture->vkImageView(), m_normalTexture->vkSampler(),
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                                      .addImage(m_ormTexture->vkImageView(), m_ormTexture->vkSampler(),
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                                      .addImage(m_emissiveTexture->vkImageView(), m_emissiveTexture->vkSampler(),
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 3)
                                      .addBuffer(m_paramBuffers[i], sizeof(GPUParams), 4,
                                                 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                      .build(m_device, descriptorPool,
                                             EngineShaderFamilies::materialDescriptorSetLayout->vk());
            continue;
        }

        DescriptorSetBuilder::begin()
            .addImage(m_albedoTexture->vkImageView(), m_albedoTexture->vkSampler(),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
            .addImage(m_normalTexture->vkImageView(), m_normalTexture->vkSampler(),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
            .addImage(m_ormTexture->vkImageView(), m_ormTexture->vkSampler(),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
            .addImage(m_emissiveTexture->vkImageView(), m_emissiveTexture->vkSampler(),
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 3)
            .addBuffer(m_paramBuffers[i], sizeof(GPUParams), 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .update(m_device, m_descriptorSets[i]);
    }
}

void Material::updateParamBuffers()
{
    for (auto &paramBuffer : m_paramBuffers)
    {
        if (paramBuffer)
            paramBuffer->upload(&m_params, sizeof(GPUParams));
    }
}

VkDescriptorSet Material::getDescriptorSet(uint32_t frameIndex) const
{
    if (frameIndex < m_descriptorSets.size())
        return m_descriptorSets[frameIndex];
    return VK_NULL_HANDLE;
}

void Material::createDefaultMaterial(Texture::SharedPtr texture)
{
    s_defaultMaterial = create(texture);
}

Material::SharedPtr Material::create(Texture::SharedPtr texture)
{
    return std::make_shared<Material>(texture);
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

Texture::SharedPtr Material::getAlbedoTexture()  const { return m_albedoTexture; }
Texture::SharedPtr Material::getNormalTexture()   const { return m_normalTexture; }
Texture::SharedPtr Material::getOrmTexture()      const { return m_ormTexture; }
Texture::SharedPtr Material::getEmissiveTexture() const { return m_emissiveTexture; }

// ---------------- Params ----------------

void Material::setBaseColorFactor(const glm::vec4 &color)
{
    m_params.baseColorFactor = color;
    updateParamBuffers();
}

void Material::setEmissiveFactor(const glm::vec3 &emissive)
{
    m_params.emissiveFactor = glm::vec4(emissive, 0.0f);
    updateParamBuffers();
}

void Material::setMetallic(float metallic)
{
    m_params.metallicFactor = clamp01(metallic);
    updateParamBuffers();
}

void Material::setRoughness(float roughness)
{
    m_params.roughnessFactor = std::max(0.04f, clamp01(roughness));
    updateParamBuffers();
}

void Material::setNormalScale(float normalScale)
{
    m_params.normalScale = std::max(0.0f, normalScale);
    updateParamBuffers();
}

void Material::setAoStrength(float aoStrength)
{
    m_params.aoStrength = clamp01(aoStrength);
    updateParamBuffers();
}

void Material::setAlphaCutoff(float cutoff)
{
    m_params.alphaCutoff = clamp01(cutoff);
    updateParamBuffers();
}

void Material::setFlags(uint32_t flags)
{
    m_params.flags = flags;
    updateParamBuffers();
}

void Material::setIor(float ior)
{
    m_params.ior = ior;
    updateParamBuffers();
}

void Material::setUVScale(const glm::vec2 &scale)
{
    m_params.uvTransform.x = scale.x;
    m_params.uvTransform.y = scale.y;
    updateParamBuffers();
}

void Material::setUVOffset(const glm::vec2 &offset)
{
    m_params.uvTransform.z = offset.x;
    m_params.uvTransform.w = offset.y;
    updateParamBuffers();
}

void Material::setUVRotation(float rotationDegrees)
{
    m_params.uvRotation = rotationDegrees;
    updateParamBuffers();
}

const Material::GPUParams &Material::params() const { return m_params; }

const std::string &Material::getCustomFragPath() const { return m_customFragPath; }
void Material::setCustomFragPath(const std::string &path) { m_customFragPath = path; }

void Material::setDomain(MaterialDomain domain) { m_domain = domain; }
MaterialDomain Material::getDomain() const { return m_domain; }
void Material::setDecalBlendMode(DecalBlendMode blendMode) { m_decalBlendMode = blendMode; }
DecalBlendMode Material::getDecalBlendMode() const { return m_decalBlendMode; }

ELIX_NESTED_NAMESPACE_END
