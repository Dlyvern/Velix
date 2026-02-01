#ifndef ELIX_ASSETS_LOADER_HPP
#define ELIX_ASSETS_LOADER_HPP

#include "Core/Macros.hpp"

#include "Engine/Mesh.hpp"
#include "Engine/Assets/IAssetLoader.hpp"
#include "Engine/Skeleton.hpp"

#include <string>
#include <vector>
#include <optional>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class AssetsLoader
{
public:
    static void registerAssetLoader(const std::shared_ptr<IAssetLoader> &assetLoader);
    static std::optional<ModelAsset> loadModel(const std::string &path);
    static std::optional<MaterialAsset> loadMaterial(const std::string &path);

public:
    static inline std::vector<std::shared_ptr<IAssetLoader>> s_assetLoaders;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASSETS_LOADER_HPP