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
    FbxManager* m_fbxManager{nullptr};
    FbxIOSettings* m_fbxIOSettings{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_FBX_ASSET_LOADER_HPP