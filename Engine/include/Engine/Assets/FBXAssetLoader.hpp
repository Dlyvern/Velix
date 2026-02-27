#ifndef ELIX_FBX_ASSET_LOADER_HPP
#define ELIX_FBX_ASSET_LOADER_HPP

#include "Engine/Assets/IAssetLoader.hpp"
#include "Engine/Skeleton.hpp"
#include "Engine/Components/AnimatorComponent.hpp"

#include <fbxsdk.h>

#include <optional>
#include <filesystem>
#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class FBXAssetLoader : public IAssetLoader
{
public:
    FBXAssetLoader();
    ~FBXAssetLoader() override;

    const std::vector<std::string> getSupportedFormats() const override;
    std::shared_ptr<IAsset> load(const std::string &filePath) override;

private:
    struct ImportedMaterialData
    {
        std::string name;
        std::string albedoTexture;
        std::string normalTexture;
        std::string emissiveTexture;
    };

    struct ProceedingMeshData
    {
        std::vector<CPUMesh> meshes;
        std::optional<Skeleton> skeleton{std::nullopt};
    };

    std::shared_ptr<IAsset> loadInternal(const std::string &filePath);
    void processNode(FbxNode *node, ProceedingMeshData &meshData);
    void processNodeAttribute(FbxNodeAttribute *nodeAttribute, FbxNode *node, ProceedingMeshData &meshData);
    void processMaterials(FbxNode *node, CPUMesh &mesh);
    std::optional<Skeleton> processSkeleton(FbxMesh *mesh);
    void processMesh(FbxNode *node, FbxMesh *mesh, ProceedingMeshData &meshData);
    void processFbxSkeleton(FbxNode *node, FbxSkeleton *skeleton);
    std::vector<Animation> processAnimations(FbxScene *scene, std::optional<Skeleton> &skeleton);
    ImportedMaterialData parseMaterial(FbxSurfaceMaterial *material);
    std::string extractTexturePathFromProperty(FbxProperty property);
    std::string normalizeTexturePath(const std::string &rawPath);
    void resetImportCaches();

    FbxManager *m_fbxManager{nullptr};
    FbxIOSettings *m_fbxIOSettings{nullptr};
    std::filesystem::path m_currentAssetDirectory;
    std::unordered_map<const FbxSurfaceMaterial *, ImportedMaterialData> m_materialImportCache;
    std::unordered_map<std::string, std::string> m_texturePathResolveCache;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_FBX_ASSET_LOADER_HPP
