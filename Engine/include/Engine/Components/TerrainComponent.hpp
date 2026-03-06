#ifndef ELIX_TERRAIN_COMPONENT_HPP
#define ELIX_TERRAIN_COMPONENT_HPP

#include "Core/Macros.hpp"

#include "Engine/Components/ECS.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/Terrain/TerrainAsset.hpp"

#include <memory>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class TerrainComponent : public ECS
{
public:
    TerrainComponent();
    explicit TerrainComponent(std::shared_ptr<TerrainAsset> terrainAsset);

    void setTerrainAsset(std::shared_ptr<TerrainAsset> terrainAsset);
    std::shared_ptr<TerrainAsset> getTerrainAsset() const;

    void setTerrainAssetPath(const std::string &terrainAssetPath);
    const std::string &getTerrainAssetPath() const;

    void setMaterialOverridePath(const std::string &materialPath);
    const std::string &getMaterialOverridePath() const;

    void setQuadsPerChunk(uint32_t quadsPerChunk);
    uint32_t getQuadsPerChunk() const;

    void setChunksDirty();
    void ensureChunkMeshesBuilt();

    const std::vector<CPUMesh> &getChunkMeshes() const;

private:
    void rebuildChunkMeshes();

private:
    std::shared_ptr<TerrainAsset> m_terrainAsset{nullptr};
    std::string m_terrainAssetPath{};
    std::string m_materialOverridePath{};

    uint32_t m_quadsPerChunk{63u};
    bool m_chunkMeshesDirty{true};
    std::vector<CPUMesh> m_chunkMeshes;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_TERRAIN_COMPONENT_HPP
