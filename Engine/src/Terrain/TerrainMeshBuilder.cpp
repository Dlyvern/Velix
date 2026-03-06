#include "Engine/Terrain/TerrainMeshBuilder.hpp"

#include "Engine/Vertex.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace
{
    float safeHeight(const TerrainAsset &terrainAsset, int32_t x, int32_t y)
    {
        if (!terrainAsset.isValid())
            return 0.0f;

        const int32_t maxX = static_cast<int32_t>(terrainAsset.width) - 1;
        const int32_t maxY = static_cast<int32_t>(terrainAsset.height) - 1;
        x = std::clamp(x, 0, maxX);
        y = std::clamp(y, 0, maxY);
        return terrainAsset.sampleWorldHeight(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    }

    glm::vec3 computeNormal(const TerrainAsset &terrainAsset, int32_t x, int32_t y, float gridStepX, float gridStepZ)
    {
        const float hL = safeHeight(terrainAsset, x - 1, y);
        const float hR = safeHeight(terrainAsset, x + 1, y);
        const float hD = safeHeight(terrainAsset, x, y - 1);
        const float hU = safeHeight(terrainAsset, x, y + 1);

        const float dx = hR - hL;
        const float dz = hU - hD;

        glm::vec3 normal(-dx / std::max(gridStepX * 2.0f, 0.0001f), 1.0f, -dz / std::max(gridStepZ * 2.0f, 0.0001f));
        const float length = glm::length(normal);
        if (length <= std::numeric_limits<float>::epsilon())
            return {0.0f, 1.0f, 0.0f};

        return normal / length;
    }
} // namespace

std::vector<CPUMesh> TerrainMeshBuilder::buildChunkMeshes(const TerrainAsset &terrainAsset,
                                                          const TerrainMeshBuildSettings &settings)
{
    std::vector<CPUMesh> meshes;
    if (!terrainAsset.isValid())
        return meshes;

    const uint32_t quadsX = terrainAsset.width - 1u;
    const uint32_t quadsY = terrainAsset.height - 1u;
    if (quadsX == 0u || quadsY == 0u)
        return meshes;

    const uint32_t quadsPerChunk = std::clamp(settings.quadsPerChunk, 1u, std::max(quadsX, quadsY));
    const uint32_t chunkCountX = (quadsX + quadsPerChunk - 1u) / quadsPerChunk;
    const uint32_t chunkCountY = (quadsY + quadsPerChunk - 1u) / quadsPerChunk;
    meshes.reserve(static_cast<size_t>(chunkCountX) * static_cast<size_t>(chunkCountY));

    const float worldMinX = -terrainAsset.worldSizeX * 0.5f;
    const float worldMinZ = -terrainAsset.worldSizeZ * 0.5f;
    const float gridStepX = terrainAsset.worldSizeX / static_cast<float>(quadsX);
    const float gridStepZ = terrainAsset.worldSizeZ / static_cast<float>(quadsY);

    for (uint32_t chunkY = 0; chunkY < chunkCountY; ++chunkY)
    {
        for (uint32_t chunkX = 0; chunkX < chunkCountX; ++chunkX)
        {
            const uint32_t startX = chunkX * quadsPerChunk;
            const uint32_t startY = chunkY * quadsPerChunk;
            const uint32_t endX = std::min(startX + quadsPerChunk, quadsX);
            const uint32_t endY = std::min(startY + quadsPerChunk, quadsY);

            const uint32_t vertsX = (endX - startX) + 1u;
            const uint32_t vertsY = (endY - startY) + 1u;
            const uint32_t localQuadCountX = vertsX - 1u;
            const uint32_t localQuadCountY = vertsY - 1u;

            std::vector<vertex::Vertex3D> vertices;
            vertices.resize(static_cast<size_t>(vertsX) * static_cast<size_t>(vertsY));

            for (uint32_t localY = 0; localY < vertsY; ++localY)
            {
                for (uint32_t localX = 0; localX < vertsX; ++localX)
                {
                    const uint32_t sampleX = startX + localX;
                    const uint32_t sampleY = startY + localY;

                    const float worldX = worldMinX + static_cast<float>(sampleX) * gridStepX;
                    const float worldY = terrainAsset.sampleWorldHeight(sampleX, sampleY);
                    const float worldZ = worldMinZ + static_cast<float>(sampleY) * gridStepZ;

                    const float u = static_cast<float>(sampleX) / static_cast<float>(quadsX);
                    const float v = static_cast<float>(sampleY) / static_cast<float>(quadsY);

                    const glm::vec3 normal = computeNormal(
                        terrainAsset,
                        static_cast<int32_t>(sampleX),
                        static_cast<int32_t>(sampleY),
                        gridStepX,
                        gridStepZ);

                    const glm::vec3 tangent = glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f));
                    const glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));

                    const size_t vertexIndex = static_cast<size_t>(localY) * static_cast<size_t>(vertsX) + static_cast<size_t>(localX);
                    vertices[vertexIndex] = vertex::Vertex3D({worldX, worldY, worldZ}, {u, v}, normal, tangent, bitangent);
                }
            }

            std::vector<uint32_t> indices;
            indices.reserve(static_cast<size_t>(localQuadCountX) * static_cast<size_t>(localQuadCountY) * 6u);

            for (uint32_t localY = 0; localY < localQuadCountY; ++localY)
            {
                for (uint32_t localX = 0; localX < localQuadCountX; ++localX)
                {
                    const uint32_t topLeft = localY * vertsX + localX;
                    const uint32_t topRight = topLeft + 1u;
                    const uint32_t bottomLeft = topLeft + vertsX;
                    const uint32_t bottomRight = bottomLeft + 1u;

                    indices.push_back(topLeft);
                    indices.push_back(bottomLeft);
                    indices.push_back(topRight);

                    indices.push_back(topRight);
                    indices.push_back(bottomLeft);
                    indices.push_back(bottomRight);
                }
            }

            CPUMesh mesh = CPUMesh::build(vertices, indices);
            mesh.name = "TerrainChunk_" + std::to_string(chunkX) + "_" + std::to_string(chunkY);

            if (!terrainAsset.layers.empty())
            {
                const TerrainLayerInfo &baseLayer = terrainAsset.layers.front();
                mesh.material.name = baseLayer.name;
                mesh.material.albedoTexture = baseLayer.albedoTexture;
                mesh.material.normalTexture = baseLayer.normalTexture;
                mesh.material.ormTexture = baseLayer.ormTexture;
                mesh.material.uvScale = glm::vec2(baseLayer.uvScale);
            }

            meshes.push_back(std::move(mesh));
        }
    }

    return meshes;
}

ELIX_NESTED_NAMESPACE_END
