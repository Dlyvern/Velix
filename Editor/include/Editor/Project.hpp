#ifndef PROJECT_HPP
#define PROJECT_HPP

#include <string>
#include <vector>
#include <unordered_map>

#include "Core/Macros.hpp"

#include "Engine/PluginSystem/PluginLoader.hpp"
#include "Engine/Assets/AssetsCache.hpp"
#include "Engine/Material.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

struct TextureAssetRecord
{
    std::string path;
    engine::Texture::SharedPtr gpu;
    bool loaded = false;
    VkDescriptorSet previewDescriptorSet{VK_NULL_HANDLE}; // TODO FOR NOW DELETE THIS SHIT
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

struct ProjectCache
{
    std::unordered_map<std::string, MaterialAssetRecord> materialsByPath;
    std::unordered_map<std::string, TextureAssetRecord> texturesByPath;
};

struct Project
{
public:
    ProjectCache cache;
    engine::LibraryHandle projectLibrary;
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
    }
};

ELIX_NESTED_NAMESPACE_END

#endif // PROJECT_HPP
