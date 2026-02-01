#ifndef ELIX_FBX_ASSET_LOADER_HPP
#define ELIX_FBX_ASSET_LOADER_HPP

#include "Engine/Assets/IAssetLoader.hpp"
#include "Engine/Skeleton.hpp"
#include "Engine/Components/AnimatorComponent.hpp"

#include <fbxsdk.h>

#include <optional>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class FBXAssetLoader : public IAssetLoader
{
public:
    FBXAssetLoader();
    const std::vector<std::string> getSupportedFormats() const override;
    std::shared_ptr<IAsset> load(const std::string &filePath) override;

private:
    struct ProceedingMeshData
    {
        std::vector<CPUMesh> meshes;
        std::optional<Skeleton> skeleton{std::nullopt};
    };

    void processNode(FbxNode *node, ProceedingMeshData &meshData);
    void processNodeAttribute(FbxNodeAttribute *nodeAttribute, FbxNode *node, ProceedingMeshData &meshData);
    void processMaterials(FbxNode *node, CPUMesh &mesh);
    std::optional<Skeleton> processSkeleton(FbxMesh *mesh);
    void processMesh(FbxNode *node, FbxMesh *mesh, ProceedingMeshData &meshData);
    std::vector<Animation> processAnimations(FbxScene *scene);

    FbxManager *m_fbxManager{nullptr};
    FbxIOSettings *m_fbxIOSettings{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_FBX_ASSET_LOADER_HPP