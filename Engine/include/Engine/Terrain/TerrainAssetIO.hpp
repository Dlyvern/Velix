#ifndef ELIX_TERRAIN_ASSET_IO_HPP
#define ELIX_TERRAIN_ASSET_IO_HPP

#include "Core/Macros.hpp"
#include "Engine/Terrain/TerrainAsset.hpp"

#include <optional>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

bool saveTerrainAssetToFile(const TerrainAsset &terrainAsset, const std::string &filePath);
std::optional<TerrainAsset> loadTerrainAssetFromFile(const std::string &filePath);

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_TERRAIN_ASSET_IO_HPP
