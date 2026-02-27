#ifndef ELIX_ASSET_HPP
#define ELIX_ASSET_HPP

#include "Core/Macros.hpp"

#include "Engine/Mesh.hpp"
#include "Engine/Skeleton.hpp"
#include "Engine/Material.hpp"
#include "Engine/Components/AnimatorComponent.hpp"

#include <string>
#include <cstdint>
#include <array>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class IAsset
{
public:
    virtual ~IAsset() = default;
};

class MaterialAsset : public IAsset
{
public:
    CPUMaterial material;

    MaterialAsset(const CPUMaterial &material) : material(material)
    {
    }
};

class TextureAsset : public IAsset
{
public:
    enum class PixelEncoding : uint8_t
    {
        RGBA8 = 0,
        RGBA32F = 1,
        COMPRESSED_GPU = 2
    };

    std::string name;
    std::string sourcePath;
    std::string assetPath;
    uint32_t width{0};
    uint32_t height{0};
    uint32_t channels{4};
    PixelEncoding encoding{PixelEncoding::RGBA8};
    uint32_t vkFormat{0};
    std::vector<uint8_t> pixels;
};

class ModelAsset : public IAsset
{
public:
    std::string sourcePath;
    std::string assetPath;
    std::vector<CPUMesh> meshes;
    std::vector<std::string> materialPaths;
    std::optional<Skeleton> skeleton{std::nullopt};
    std::vector<Animation> animations;

    ModelAsset(const std::vector<CPUMesh> &meshes, const std::optional<Skeleton> skeleton = std::nullopt, const std::vector<Animation> &animations = {})
        : meshes(meshes),
          skeleton(skeleton),
          animations(animations)
    {
    }
};

class Asset
{
public:
    static constexpr std::array<char, 4> MAGIC{'E', 'L', 'X', 'A'};
    static constexpr uint32_t VERSION = 1u;

    enum class AssetType : uint8_t
    {
        NONE = 0,
        TEXTURE = 1,
        MODEL = 2,
        MATERIAL = 3
    };

    struct BinaryHeader
    {
        char magic[4];
        uint32_t version;
        uint8_t type;
        uint8_t reserved[3];
        uint64_t payloadSize;
    };

    struct AssetMeta
    {
        char magic[4];
        std::string name;
        AssetType type{AssetType::NONE};
        uint32_t version{VERSION};
        uint64_t payloadSize{0};
        std::string sourcePath;
        std::string serializedPath;
    };

    AssetMeta meta;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASSET_HPP
