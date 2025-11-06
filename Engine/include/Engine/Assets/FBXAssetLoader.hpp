#ifndef ELIX_FBX_ASSET_LOADER_HPP
#define ELIX_FBX_ASSET_LOADER_HPP

#include "Engine/Assets/IAssetLoader.hpp"

#include <fbxsdk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class FBXAssetLoader : public IAssetLoader
{
public:
    FBXAssetLoader();
    const std::vector<std::string> getSupportedFormats() const override;
    std::shared_ptr<IAsset> load(const std::string& filePath) override;
private:
    void processNode(FbxNode* node, std::vector<Mesh3D>& meshes);
    void processNodeAttribute(FbxNodeAttribute* nodeAttribute, FbxNode* node, std::vector<Mesh3D>& meshes);
    void processMaterials(FbxNode* node, Mesh3D& mesh);

    void processMesh(FbxNode* node, FbxMesh* mesh, std::vector<Mesh3D>& meshes);

    FbxManager* m_fbxManager{nullptr};
    FbxIOSettings* m_fbxIOSettings{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_FBX_ASSET_LOADER_HPP