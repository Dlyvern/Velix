#include "Engine/Assets/TerrainAssetLoader.hpp"

#include "Core/Logger.hpp"
#include "Engine/Terrain/TerrainAssetIO.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

const std::vector<std::string> TerrainAssetLoader::getSupportedFormats() const
{
    return {".elixterrain", ".elixterrain.json"};
}

std::shared_ptr<IAsset> TerrainAssetLoader::load(const std::string &filePath)
{
    auto terrainAsset = loadTerrainAssetFromFile(filePath);
    if (!terrainAsset.has_value())
    {
        VX_ENGINE_ERROR_STREAM("Failed to load terrain asset file: " << filePath << '\n');
        return nullptr;
    }

    return std::make_shared<TerrainAsset>(std::move(terrainAsset.value()));
}

ELIX_NESTED_NAMESPACE_END
