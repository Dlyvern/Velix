#ifndef ELIX_MATERIAL_HPP
#define ELIX_MATERIAL_HPP

#include "Core/Macros.hpp"

#include "Engine/Texture.hpp"

#include <cstdint>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Material
{
public:
    using SharedPtr = std::shared_ptr<Material>;

    enum MaterialFlags : uint8_t
    {
        EMATERIAL_FLAG_NONE = 0,
        EMATERIAL_FLAG_ALPHA_MASK = 1 << 0,
        EMATERIAL_FLAG_ALPHA_BLEND = 1 << 1
    };

    struct GPUParams
    {
        glm::vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f}; // rgba
        glm::vec4 emissiveFactor{0.0f, 0.0f, 0.0f, 0.0f};  // rgb used
        glm::vec4 uvTransform{1.0f, 1.0f, 0.0f, 0.0f};     // xy=scale, zw=offset

        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        float normalScale = 1.0f;
        float aoStrength = 1.0f;

        uint32_t flags = MaterialFlags::EMATERIAL_FLAG_NONE;
        float alphaCutoff = 0.5f;
        glm::vec2 _padding = {0.0f, 0.0f}; // keep alignment safe (std140)
    };

    Material(Texture::SharedPtr texture);

    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const;

    void setAlbedoTexture(Texture::SharedPtr texture);
    void setNormalTexture(Texture::SharedPtr texture);
    void setOrmTexture(Texture::SharedPtr texture);
    void setEmissiveTexture(Texture::SharedPtr texture);

    Texture::SharedPtr getAlbedoTexture() const;
    Texture::SharedPtr getNormalTexture() const;
    Texture::SharedPtr getOrmTexture() const;
    Texture::SharedPtr getEmissiveTexture() const;

    void setBaseColorFactor(const glm::vec4 &color);
    void setEmissiveFactor(const glm::vec3 &emissive);
    void setMetallic(float metallic);
    void setRoughness(float roughness);
    void setNormalScale(float normalScale);
    void setAoStrength(float aoStrength);
    void setUVScale(const glm::vec2 &scale);
    void setUVOffset(const glm::vec2 &offset);
    void setAlphaCutoff(float cutoff);
    void setFlags(uint32_t flags);

    const GPUParams &params() const;

    static Material::SharedPtr getDefaultMaterial()
    {
        return s_defaultMaterial;
    }

    static void createDefaultMaterial(Texture::SharedPtr texture);
    static void deleteDefaultMaterial();
    static SharedPtr create(Texture::SharedPtr texture);

    void uploadParams();

private:
    void createDescriptorSets();
    void updateTextureDescriptors();

    uint32_t m_maxFramesInFlight{0};
    VkDevice m_device{VK_NULL_HANDLE};

    static inline Material::SharedPtr s_defaultMaterial{nullptr};

    GPUParams m_params{};

    Texture::SharedPtr m_albedoTexture;
    Texture::SharedPtr m_normalTexture;
    Texture::SharedPtr m_ormTexture;
    Texture::SharedPtr m_emissiveTexture;

    std::vector<VkDescriptorSet> m_descriptorSets;
    std::vector<core::Buffer::SharedPtr> m_paramBuffers;
};

class CPUMaterial
{
public:
    uint32_t flags{Material::MaterialFlags::EMATERIAL_FLAG_NONE};
    std::string albedoTexture;
    std::string normalTexture;
    std::string ormTexture;
    std::string emissiveTexture;
    std::string name;
    glm::vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 emissiveFactor{0.0f, 0.0f, 0.0f};
    float metallicFactor{0.0f};
    float roughnessFactor{1.0f};
    float aoStrength{1.0f};
    float normalScale{1.0f};
    float alphaCutoff{0.5f};

    glm::vec2 uvScale{1.0f, 1.0f};
    glm::vec2 uvOffset{0.0f, 0.0f};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_MATERIAL_HPP
