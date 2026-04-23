#include "Engine/Assets/LightmapUVGenerator.hpp"
#include "Engine/Vertex.hpp"
#include "Core/Logger.hpp"

#include <xatlas.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void LightmapUVGenerator::generate(CPUMesh &mesh)
{
    if (mesh.vertexData.empty() || mesh.indices.empty())
        return;



    const uint32_t vertexCount = static_cast<uint32_t>(mesh.vertexData.size() / mesh.vertexStride);
    if (vertexCount == 0)
        return;

    xatlas::Atlas *atlas = xatlas::Create();

    xatlas::MeshDecl decl{};
    decl.vertexCount        = vertexCount;
    decl.vertexPositionData = mesh.vertexData.data() + offsetof(vertex::Vertex3D, position);
    decl.vertexPositionStride = static_cast<uint32_t>(mesh.vertexStride);
    decl.vertexNormalData   = mesh.vertexData.data() + offsetof(vertex::Vertex3D, normal);
    decl.vertexNormalStride = static_cast<uint32_t>(mesh.vertexStride);
    decl.indexCount         = static_cast<uint32_t>(mesh.indices.size());
    decl.indexData          = mesh.indices.data();
    decl.indexFormat        = xatlas::IndexFormat::UInt32;

    const xatlas::AddMeshError err = xatlas::AddMesh(atlas, decl);
    if (err != xatlas::AddMeshError::Success)
    {
        VX_ENGINE_WARNING_STREAM("[LightmapUVGenerator] xatlas::AddMesh failed for mesh \""
                                 << mesh.name << "\": " << xatlas::StringForEnum(err) << '\n');
        xatlas::Destroy(atlas);
        return;
    }

    xatlas::ChartOptions chartOptions{};
    xatlas::PackOptions  packOptions{};
    packOptions.padding  = 2;
    xatlas::Generate(atlas, chartOptions, packOptions);

    if (atlas->meshCount == 0 || atlas->meshes == nullptr)
    {
        VX_ENGINE_WARNING_STREAM("[LightmapUVGenerator] xatlas produced no output for mesh \""
                                 << mesh.name << "\"\n");
        xatlas::Destroy(atlas);
        return;
    }

    const xatlas::Mesh &outMesh = atlas->meshes[0];


    const uint32_t newVertexCount = outMesh.vertexCount;
    const uint32_t newIndexCount  = outMesh.indexCount;

    std::vector<uint8_t> newVertexData(newVertexCount * mesh.vertexStride);
    std::vector<uint32_t> newIndices(newIndexCount);
    std::vector<glm::vec2> newLightmapUVs(newVertexCount);


    const float invW = (atlas->width  > 0) ? 1.0f / static_cast<float>(atlas->width)  : 1.0f;
    const float invH = (atlas->height > 0) ? 1.0f / static_cast<float>(atlas->height) : 1.0f;

    for (uint32_t i = 0; i < newVertexCount; ++i)
    {
        const xatlas::Vertex &v = outMesh.vertexArray[i];

        std::memcpy(newVertexData.data() + i * mesh.vertexStride,
                    mesh.vertexData.data() + v.xref * mesh.vertexStride,
                    mesh.vertexStride);
        newLightmapUVs[i] = { v.uv[0] * invW, v.uv[1] * invH };
    }

    for (uint32_t i = 0; i < newIndexCount; ++i)
        newIndices[i] = outMesh.indexArray[i];

    mesh.vertexData   = std::move(newVertexData);
    mesh.indices      = std::move(newIndices);
    mesh.lightmapUVs  = std::move(newLightmapUVs);
    mesh.invalidateGeometryInfo();

    VX_ENGINE_INFO_STREAM("[LightmapUVGenerator] Generated lightmap UVs for mesh \""
                          << mesh.name << "\": " << newVertexCount << " verts, "
                          << "atlas " << atlas->width << 'x' << atlas->height << '\n');

    xatlas::Destroy(atlas);
}

ELIX_NESTED_NAMESPACE_END
