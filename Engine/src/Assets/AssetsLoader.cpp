#include "Engine/Assets/AssetsLoader.hpp"

#include <iostream>
#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void AssetsLoader::registerAssetLoader(const std::shared_ptr<IAssetLoader>& assetLoader)
{
    s_assetLoaders.push_back(assetLoader);
}

std::vector<Mesh3D> AssetsLoader::loadModel(const std::string& path)
{
    const std::string extension = std::filesystem::path(path).extension().string();
    
    for(const auto& assetLoader : s_assetLoaders)
        if(assetLoader->canLoad(extension))
        {
            auto modelAsset = assetLoader->load(path);
            auto model = dynamic_cast<ModelAsset*>(modelAsset.get());

            if(model)
                return model->meshes;

            //!Do not break here, try to load a model with the different loaders
        }

    std::cerr << "Failed to load a model" << std::endl;

    return {};
}

ELIX_NESTED_NAMESPACE_END