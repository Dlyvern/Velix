#ifndef ELIX_MATERIAL_HPP
#define ELIX_MATERIAL_HPP

#include "Core/Macros.hpp"

#include "Engine/Texture.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

enum class MaterialDomain : uint8_t
{
    Surface = 0,
    DeferredDecal = 1
};

enum class DecalBlendMode : uint8_t
{
    ColorNormal = 0,
    ColorOnly = 1,
    NormalOnly = 2,
    Emissive = 3
};

class Material
{
public:
    using SharedPtr = std::shared_ptr<Material>;

    enum MaterialFlags : uint8_t
    {
        EMATERIAL_FLAG_NONE = 0,
        EMATERIAL_FLAG_ALPHA_MASK = 1 << 0,
        EMATERIAL_FLAG_ALPHA_BLEND = 1 << 1,


        EMATERIAL_FLAG_LEGACY_GLASS = 1 << 2,
        EMATERIAL_FLAG_DOUBLE_SIDED = 1 << 3,

        EMATERIAL_FLAG_FLIP_V = 1 << 4,

        EMATERIAL_FLAG_FLIP_U = 1 << 5,

        EMATERIAL_FLAG_CLAMP_UV = 1 << 6,
    };

    struct GPUParams
    {
        glm::vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 emissiveFactor{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 uvTransform{1.0f, 1.0f, 0.0f, 0.0f};

        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        float normalScale = 1.0f;
        float aoStrength = 1.0f;

        uint32_t flags = MaterialFlags::EMATERIAL_FLAG_NONE;
        float alphaCutoff = 0.5f;
        float uvRotation = 0.0f;
        float ior = 1.5f;


        uint32_t albedoTexIdx{0};
        uint32_t normalTexIdx{0};
        uint32_t ormTexIdx{0};
        uint32_t emissiveTexIdx{0};

        uint32_t lightmapTexIdx{0xFFFFFFFFu};
        uint32_t _lightmapPad{0};
        uint32_t _pad0{0};
        uint32_t _pad1{0};
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
    void setUVRotation(float rotationDegrees);
    void setAlphaCutoff(float cutoff);
    void setFlags(uint32_t flags);
    void setIor(float ior);
    void setDomain(MaterialDomain domain);
    MaterialDomain getDomain() const;
    void setDecalBlendMode(DecalBlendMode blendMode);
    DecalBlendMode getDecalBlendMode() const;

    const GPUParams &params() const;

    static Material::SharedPtr getDefaultMaterial()
    {
        return s_defaultMaterial;
    }

    static void createDefaultMaterial(Texture::SharedPtr texture);
    static void deleteDefaultMaterial();
    static SharedPtr create(Texture::SharedPtr texture);

    const std::string &getCustomFragPath() const;
    void setCustomFragPath(const std::string &path);

private:
    void createDescriptorSets();
    void updateTextureDescriptors();
    void updateParamBuffers();

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

    std::string m_customFragPath;
    MaterialDomain m_domain{MaterialDomain::Surface};
    DecalBlendMode m_decalBlendMode{DecalBlendMode::ColorNormal};
};

class CPUMaterial
{
public:
    uint32_t flags{Material::MaterialFlags::EMATERIAL_FLAG_NONE};
    std::string albedoTexture;
    std::string normalTexture;
    std::string ormTexture;
    std::string emissiveTexture;


    std::string lightmapTexture;
    std::string name;
    MaterialDomain domain{MaterialDomain::Surface};
    DecalBlendMode decalBlendMode{DecalBlendMode::ColorNormal};
    glm::vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 emissiveFactor{0.0f, 0.0f, 0.0f};
    float metallicFactor{0.0f};
    float roughnessFactor{1.0f};
    float aoStrength{1.0f};
    float normalScale{1.0f};
    float alphaCutoff{0.5f};
    float ior{1.5f};

    glm::vec2 uvScale{1.0f, 1.0f};
    glm::vec2 uvOffset{0.0f, 0.0f};
    float uvRotation{0.0f};



    std::string customExpression;

    std::string customShaderHash;


    struct NoiseNodeParams
    {
        enum class Type : uint8_t { Value = 0, Gradient, FBM, Voronoi };
        enum class BlendMode : uint8_t { Replace = 0, Multiply, Add };

        Type type = Type::FBM;
        BlendMode blendMode = BlendMode::Replace;
        float scale = 1.0f;
        int octaves = 4;
        float persistence = 0.5f;
        float lacunarity = 2.0f;
        bool worldSpace = false;
        bool active = true;


        std::string targetSlot{"roughness"};


        glm::vec3 rampColorA{0.0f, 0.0f, 0.0f};
        glm::vec3 rampColorB{1.0f, 1.0f, 1.0f};
    };
    std::vector<NoiseNodeParams> noiseNodes;

    struct ColorNodeParams
    {
        enum class BlendMode : uint8_t { Replace = 0, Multiply, Add };

        BlendMode blendMode = BlendMode::Multiply;
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float strength{1.0f};
        bool active = true;


        std::string targetSlot{"albedo"};
    };
    std::vector<ColorNodeParams> colorNodes;
};

ELIX_NESTED_NAMESPACE_END

#endif
