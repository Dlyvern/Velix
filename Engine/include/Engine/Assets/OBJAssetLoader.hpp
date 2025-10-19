#ifndef ELIX_OBJ_ASSET_LOADER_HPP
#define ELIX_OBJ_ASSET_LOADER_HPP

#include "Engine/Assets/IAssetLoader.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class OBJAssetLoader : public IAssetLoader
{
public:
    const std::vector<std::string> getSupportedFormats() const override;
    std::shared_ptr<IAsset> load(const std::string& filePath) override;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_OBJ_ASSET_LOADER_HPP