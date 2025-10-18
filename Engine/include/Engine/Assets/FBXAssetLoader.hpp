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
    void load(const std::string& filePath) override;
    bool canLoad(const std::string& extension) override;
private:
    // FBXManager* m_fbxManager{nullptr};
    // FbxIOSettings* m_fbxIOSettings{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_FBX_ASSET_LOADER_HPP