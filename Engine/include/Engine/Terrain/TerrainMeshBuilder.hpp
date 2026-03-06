#ifndef ELIX_TERRAIN_MESH_BUILDER_HPP
#define ELIX_TERRAIN_MESH_BUILDER_HPP

#include "Core/Macros.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/Terrain/TerrainAsset.hpp"

#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct TerrainMeshBuildSettings
{
    uint32_t quadsPerChunk{63};
    bool generateFlatShadingNormals{false};
};

class TerrainMeshBuilder
{
public:
    static std::vector<CPUMesh> buildChunkMeshes(const TerrainAsset &terrainAsset,
                                                 const TerrainMeshBuildSettings &settings = TerrainMeshBuildSettings{});
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_TERRAIN_MESH_BUILDER_HPP
