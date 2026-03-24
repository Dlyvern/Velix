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

class Material
{
public:
    using SharedPtr = std::shared_ptr<Material>;

    enum MaterialFlags : uint8_t
    {
        EMATERIAL_FLAG_NONE = 0,
        EMATERIAL_FLAG_ALPHA_MASK = 1 << 0,
        EMATERIAL_FLAG_ALPHA_BLEND = 1 << 1,
        // Legacy compatibility bit from old glass pipeline path.
        // New shading flow maps this to ALPHA_BLEND during material sanitization.
        EMATERIAL_FLAG_LEGACY_GLASS = 1 << 2,
        EMATERIAL_FLAG_DOUBLE_SIDED = 1 << 3,
        // Flip texture V coordinate (Blender/OpenGL-style UV orientation).
        EMATERIAL_FLAG_FLIP_V = 1 << 4,
        // Flip texture U coordinate (horizontal mirror fix).
        EMATERIAL_FLAG_FLIP_U = 1 << 5,
        // Clamp UV coordinates to [0..1] to avoid tiling/repeat.
        EMATERIAL_FLAG_CLAMP_UV = 1 << 6,
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
        float uvRotation = 0.0f; // degrees
        float ior = 1.5f;        // Index of Refraction (glass=1.5, water=1.33, diamond=2.4)

        // Bindless texture indices — filled by RenderGraph when registering the material.
        uint32_t albedoTexIdx{0};
        uint32_t normalTexIdx{0};
        uint32_t ormTexIdx{0};
        uint32_t emissiveTexIdx{0};
    };

    Material(Texture::SharedPtr texture);

    // Returns a per-frame descriptor set using the old per-material layout.
    // Used by editor preview pass; GBuffer uses the bindless path instead.
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
    float ior{1.5f};

    glm::vec2 uvScale{1.0f, 1.0f};
    glm::vec2 uvOffset{0.0f, 0.0f};
    float uvRotation{0.0f}; // degrees

    // Custom shader expression — GLSL source written in the material editor.
    // Empty means "use the default gbuffer_static pipeline".
    std::string customExpression;
    // FNV-1a hex hash of customExpression+noiseNodes; used at runtime to locate the cached .spv.
    std::string customShaderHash;

    // Procedural noise nodes defined in the material editor node graph.
    struct NoiseNodeParams
    {
        enum class Type : uint8_t { Value = 0, Gradient, FBM, Voronoi };
        enum class BlendMode : uint8_t { Replace = 0, Multiply, Add };

        Type type = Type::FBM;
        BlendMode blendMode = BlendMode::Replace;
        float scale = 1.0f;
        int octaves = 4;          // FBM types only
        float persistence = 0.5f; // FBM types only
        float lacunarity = 2.0f;  // FBM types only
        bool worldSpace = false;  // use stable world-space position instead of UVs
        bool active = true;

        // Target output slot: "albedo" | "emissive" | "roughness" | "metallic" | "ao" | "alpha"
        std::string targetSlot{"roughness"};

        // Color ramp — only used when targetSlot is a color slot.
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

        // Target output slot: "albedo" | "emissive"
        std::string targetSlot{"albedo"};
    };
    std::vector<ColorNodeParams> colorNodes;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_MATERIAL_HPP
