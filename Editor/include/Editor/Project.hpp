#ifndef PROJECT_HPP
#define PROJECT_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <optional>

#include "Core/Macros.hpp"

#include "Engine/PluginSystem/PluginLoader.hpp"
#include "Engine/Assets/AssetsCache.hpp"
#include "Engine/Assets/IAssetLoader.hpp"
#include "Engine/Material.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

enum class TextureUsage : uint8_t
{
    Color = 0,
    Data = 1
};

struct TextureAssetRecord
{
    std::string path;
    engine::Texture::SharedPtr gpu;
    std::unordered_map<uint8_t, engine::Texture::SharedPtr> gpuVariants;
    bool loaded = false;
    VkDescriptorSet previewDescriptorSet{VK_NULL_HANDLE}; // TODO FOR NOW DELETE THIS SHIT

    engine::Texture::SharedPtr getGpuVariant(TextureUsage usage) const
    {
        auto it = gpuVariants.find(static_cast<uint8_t>(usage));
        if (it != gpuVariants.end())
            return it->second;

        if (usage == TextureUsage::Color)
            return gpu;

        return nullptr;
    }

    void setGpuVariant(TextureUsage usage, const engine::Texture::SharedPtr &texture)
    {
        if (!texture)
            return;

        gpuVariants[static_cast<uint8_t>(usage)] = texture;

        if (usage == TextureUsage::Color)
            gpu = texture;
    }
};

struct MaterialAssetRecord
{
    std::string path;
    engine::CPUMaterial cpuData;
    engine::Material::SharedPtr gpu;
    VkDescriptorSet previewDescriptorSet{VK_NULL_HANDLE}; // TODO FOR NOW DELETE THIS SHIT
    bool dirty = false;
    engine::Texture::SharedPtr texture{nullptr};
};

struct ModelAssetRecord
{
    std::string path;
    std::optional<engine::ModelAsset> cpuData{std::nullopt};
};

struct ProjectCache
{
    std::unordered_map<std::string, MaterialAssetRecord> materialsByPath;
    std::unordered_map<std::string, TextureAssetRecord> texturesByPath;
    std::unordered_map<std::string, ModelAssetRecord> modelsByPath;
};

struct Project
{
public:
    ProjectCache cache;
    engine::LibraryHandle projectLibrary{nullptr};
    std::string resourcesDir;
    std::string entryScene;
    std::string scenesDir;
    std::string name;
    std::string fullPath;
    std::string buildDir;
    std::string sourcesDir;
    std::string exportDir;

    void clearCache()
    {
        cache.materialsByPath.clear();
        cache.texturesByPath.clear();
        cache.modelsByPath.clear();
    }
};

ELIX_NESTED_NAMESPACE_END

#endif // PROJECT_HPP
