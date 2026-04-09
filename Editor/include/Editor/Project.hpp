#ifndef PROJECT_HPP
#define PROJECT_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <optional>
#include <filesystem>

#include "Core/Macros.hpp"

#include "Engine/PluginSystem/PluginLoader.hpp"
#include "Engine/Assets/AssetsCache.hpp"
#include "Engine/Assets/IAssetLoader.hpp"
#include "Engine/Material.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

enum class TextureUsage : uint8_t
{
    Color = 0,
    Data = 1,
    PreviewColor = 2,
    PreviewData = 3
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

enum class ExportTargetSelection : uint8_t
{
    Linux = 0,
    Windows = 1,
    Both = 2
};

enum class ExportPlatform : uint8_t
{
    Linux = 0,
    Windows = 1
};

struct ExportPlatformSettings
{
    std::string buildDir;
    std::string exportDir;
    std::string cmakeGenerator;
    std::string cmakeToolchainFile;
    std::string supportRootDir;
    std::string runtimeExecutablePath;
};

struct Project
{
public:
    ProjectCache cache;
    engine::LibraryHandle projectLibrary{nullptr};
    std::filesystem::path loadedProjectLibraryPath;
    bool loadedProjectLibraryIsTemporaryCopy{false};
    std::string resourcesDir;
    std::string entryScene;
    std::string scenesDir;
    std::string name;
    std::string fullPath;
    std::string configPath;
    std::string buildDir;
    std::string sourcesDir;
    std::string exportDir;
    ExportTargetSelection exportTargetSelection{
#if defined(_WIN32)
        ExportTargetSelection::Windows
#else
        ExportTargetSelection::Linux
#endif
    };
    ExportPlatformSettings linuxExport;
    ExportPlatformSettings windowsExport;

    void rememberLoadedProjectLibrary(engine::LibraryHandle library,
                                      const std::filesystem::path &libraryPath = {},
                                      bool isTemporaryCopy = false)
    {
        projectLibrary = library;
        loadedProjectLibraryPath = libraryPath;
        loadedProjectLibraryIsTemporaryCopy = isTemporaryCopy;
    }

    void unloadProjectLibrary()
    {
        if (projectLibrary)
            engine::PluginLoader::closeLibrary(projectLibrary);

        projectLibrary = nullptr;

        if (loadedProjectLibraryIsTemporaryCopy && !loadedProjectLibraryPath.empty())
        {
            std::error_code errorCode;
            std::filesystem::remove(loadedProjectLibraryPath, errorCode);

            const std::filesystem::path parentDirectory = loadedProjectLibraryPath.parent_path();
            if (!parentDirectory.empty())
                std::filesystem::remove(parentDirectory, errorCode);
        }

        loadedProjectLibraryPath.clear();
        loadedProjectLibraryIsTemporaryCopy = false;
    }

    void clearCache()
    {
        cache.materialsByPath.clear();
        cache.texturesByPath.clear();
        cache.modelsByPath.clear();
    }
};

inline bool isProjectConfigFilePath(const std::filesystem::path &path)
{
    const std::string extension = path.extension().string();
    return extension == ".elixproject" || extension == ".elixirproject";
}

inline std::filesystem::path resolveProjectRootPath(const Project &project)
{
    auto normalizeProjectRoot = [](const std::filesystem::path &candidate) -> std::filesystem::path
    {
        if (candidate.empty())
            return {};

        const std::filesystem::path normalized = candidate.lexically_normal();
        std::error_code errorCode;
        if (std::filesystem::is_directory(normalized, errorCode) && !errorCode)
            return normalized;

        errorCode.clear();
        if (std::filesystem::is_regular_file(normalized, errorCode) && !errorCode && isProjectConfigFilePath(normalized))
            return normalized.parent_path().lexically_normal();

        if (isProjectConfigFilePath(normalized))
            return normalized.parent_path().lexically_normal();

        return {};
    };

    if (const std::filesystem::path fromFullPath = normalizeProjectRoot(project.fullPath); !fromFullPath.empty())
        return fromFullPath;

    if (const std::filesystem::path fromConfigPath = normalizeProjectRoot(project.configPath); !fromConfigPath.empty())
        return fromConfigPath;

    const std::filesystem::path fallback = project.fullPath.empty()
                                               ? std::filesystem::path(project.configPath)
                                               : std::filesystem::path(project.fullPath);
    if (fallback.empty())
        return {};

    if (isProjectConfigFilePath(fallback))
        return fallback.parent_path().lexically_normal();

    return fallback.lexically_normal();
}

ELIX_NESTED_NAMESPACE_END

#endif // PROJECT_HPP
