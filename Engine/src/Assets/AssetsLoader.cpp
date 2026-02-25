#include "Engine/Assets/AssetsLoader.hpp"

#include <iostream>
#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void AssetsLoader::registerAssetLoader(const std::shared_ptr<IAssetLoader> &assetLoader)
{
    s_assetLoaders.push_back(assetLoader);
}

std::optional<MaterialAsset> AssetsLoader::loadMaterial(const std::string &path)
{
    const std::string extension = std::filesystem::path(path).extension().string();

    for (const auto &assetLoader : s_assetLoaders)
        if (assetLoader->canLoad(extension))
        {
            auto materialAsset = assetLoader->load(path);
            auto material = dynamic_cast<MaterialAsset *>(materialAsset.get());

            if (material)
                return *material;
        }

    VX_ENGINE_ERROR_STREAM("Failed to load a material\n");

    return std::nullopt;
}

std::optional<ModelAsset> AssetsLoader::loadModel(const std::string &path)
{
    const std::string extension = std::filesystem::path(path).extension().string();

    for (const auto &assetLoader : s_assetLoaders)
        if (assetLoader->canLoad(extension))
        {
            auto modelAsset = assetLoader->load(path);
            auto model = dynamic_cast<ModelAsset *>(modelAsset.get());

            if (model)
                return *model;

            //! Do not break here, try to load a model with the different loaders
        }

    VX_ENGINE_ERROR_STREAM("Failed to load a model" << std::endl);

    return std::nullopt;
}

ELIX_NESTED_NAMESPACE_END