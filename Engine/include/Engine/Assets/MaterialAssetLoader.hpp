#ifndef ELIX_MATERIAL_ASSET_LOADER_HPP
#define ELIX_MATERIAL_ASSET_LOADER_HPP

#include "Engine/Assets/IAssetLoader.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class MaterialAssetLoader final : public IAssetLoader
{
public:
    const std::vector<std::string> getSupportedFormats() const override;
    std::shared_ptr<IAsset> load(const std::string &filePath) override;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_MATERIAL_ASSET_LOADER_HPP