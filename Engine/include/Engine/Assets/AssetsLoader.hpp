#ifndef ELIX_ASSETS_LOADER_HPP
#define ELIX_ASSETS_LOADER_HPP

#include "Core/Macros.hpp"

#include "Engine/Mesh.hpp"
#include "Engine/Texture.hpp"
#include "Engine/Assets/IAssetLoader.hpp"
#include "Engine/Skeleton.hpp"
#include "Engine/Terrain/TerrainAsset.hpp"

#include <filesystem>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class AssetsLoader
{
public:
    static void registerAssetLoader(const std::shared_ptr<IAssetLoader> &assetLoader);
    static void clearAssetLoaders();
    static bool importModelAsset(const std::string &sourcePath, const std::string &outputAssetPath);
    static bool importTextureAsset(const std::string &sourcePath, const std::string &outputAssetPath);
    static bool importAudioAsset(const std::string &sourcePath, const std::string &outputAssetPath);
    static bool importAnimationFromFBX(const std::string &fbxPath, const std::string &outputAssetPath);
    static bool exportAnimationsFromModel(const std::string &modelAssetPath, const std::string &outputAssetPath);
    static std::optional<ModelAsset> loadModel(const std::string &path);
    static std::optional<TextureAsset> loadTexture(const std::string &path);
    static std::optional<AudioAsset> loadAudio(const std::string &path);
    static std::optional<AnimationAsset> loadAnimationAsset(const std::string &path);
    static std::optional<AnimationTree> loadAnimationTree(const std::string &path);
    static bool saveAnimationTree(const AnimationTree &tree, const std::string &path);
    static Texture::SharedPtr loadTextureGPU(const std::string &path,
                                             VkFormat preferredLdrFormat = VK_FORMAT_R8G8B8A8_SRGB);
    static Texture::SharedPtr createTextureGPU(const TextureAsset &textureAsset,
                                               VkFormat preferredLdrFormat = VK_FORMAT_R8G8B8A8_SRGB);
    static std::optional<MaterialAsset> loadMaterial(const std::string &path);
    static std::optional<TerrainAsset> loadTerrain(const std::string &path);
    static void setTextureAssetImportRootDirectory(const std::filesystem::path &rootDirectory);
    static std::filesystem::path getTextureAssetImportRootDirectory();

public:
    static inline std::vector<std::shared_ptr<IAssetLoader>> s_assetLoaders;
    static inline std::filesystem::path s_textureAssetImportRootDirectory{};

private:
    static std::optional<ModelAsset> importModelFromSource(const std::string &path);
    static std::optional<TextureAsset> importTextureFromSource(const std::string &path);
    static std::filesystem::path toModelAssetPath(const std::filesystem::path &sourcePath);
    static std::filesystem::path toTextureAssetPath(const std::filesystem::path &sourcePath);
    static std::filesystem::path toAudioAssetPath(const std::filesystem::path &sourcePath);
    static bool needsReimport(const std::filesystem::path &sourcePath, const std::filesystem::path &serializedPath);
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASSETS_LOADER_HPP
