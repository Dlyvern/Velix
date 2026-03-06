#include "Engine/Components/TerrainComponent.hpp"

#include "Engine/Terrain/TerrainMeshBuilder.hpp"

#include <algorithm>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

TerrainComponent::TerrainComponent()
{
}

TerrainComponent::TerrainComponent(std::shared_ptr<TerrainAsset> terrainAsset)
    : m_terrainAsset(std::move(terrainAsset))
{
}

void TerrainComponent::setTerrainAsset(std::shared_ptr<TerrainAsset> terrainAsset)
{
    m_terrainAsset = std::move(terrainAsset);
    if (m_terrainAsset && !m_terrainAsset->layers.empty() && m_materialOverridePath.empty())
        m_materialOverridePath = m_terrainAsset->layers.front().materialPath;

    setChunksDirty();
}

std::shared_ptr<TerrainAsset> TerrainComponent::getTerrainAsset() const
{
    return m_terrainAsset;
}

void TerrainComponent::setTerrainAssetPath(const std::string &terrainAssetPath)
{
    m_terrainAssetPath = terrainAssetPath;
}

const std::string &TerrainComponent::getTerrainAssetPath() const
{
    return m_terrainAssetPath;
}

void TerrainComponent::setMaterialOverridePath(const std::string &materialPath)
{
    m_materialOverridePath = materialPath;
}

const std::string &TerrainComponent::getMaterialOverridePath() const
{
    return m_materialOverridePath;
}

void TerrainComponent::setQuadsPerChunk(uint32_t quadsPerChunk)
{
    m_quadsPerChunk = std::clamp(quadsPerChunk, 1u, 512u);
    setChunksDirty();
}

uint32_t TerrainComponent::getQuadsPerChunk() const
{
    return m_quadsPerChunk;
}

void TerrainComponent::setChunksDirty()
{
    m_chunkMeshesDirty = true;
}

void TerrainComponent::ensureChunkMeshesBuilt()
{
    if (!m_chunkMeshesDirty)
        return;

    rebuildChunkMeshes();
}

const std::vector<CPUMesh> &TerrainComponent::getChunkMeshes() const
{
    return m_chunkMeshes;
}

void TerrainComponent::rebuildChunkMeshes()
{
    m_chunkMeshesDirty = false;
    m_chunkMeshes.clear();

    if (!m_terrainAsset || !m_terrainAsset->isValid())
        return;

    TerrainMeshBuildSettings buildSettings{};
    buildSettings.quadsPerChunk = m_quadsPerChunk;
    m_chunkMeshes = TerrainMeshBuilder::buildChunkMeshes(*m_terrainAsset, buildSettings);
}

ELIX_NESTED_NAMESPACE_END
