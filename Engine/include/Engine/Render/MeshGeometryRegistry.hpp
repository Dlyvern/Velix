#ifndef ELIX_MESH_GEOMETRY_REGISTRY_HPP
#define ELIX_MESH_GEOMETRY_REGISTRY_HPP

#include "Core/Macros.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/Render/UnifiedGeometryBuffer.hpp"

#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class MeshGeometryRegistry
{
public:
    struct Entry
    {
        MeshGeometryHash geometryHash{};
        GPUMesh::SharedPtr sharedMesh{nullptr};
    };

    void setUnifiedGeometryBuffer(UnifiedGeometryBuffer *unifiedGeometryBuffer,
                                  VkDeviceSize unifiedVertexBufferSize,
                                  uint32_t unifiedIndexBufferCount);

    const Entry *find(MeshGeometryHash geometryHash) const;
    Entry *find(MeshGeometryHash geometryHash);

    bool contains(MeshGeometryHash geometryHash) const;

    GPUMesh::SharedPtr getOrCreateSharedGeometryMesh(const CPUMesh &mesh);
    GPUMesh::SharedPtr createDrawMeshInstance(const CPUMesh &mesh);

    void clear();
    std::size_t size() const;

private:
    std::unordered_map<MeshGeometryHash, Entry, MeshGeometryHashHasher> m_entries;
    UnifiedGeometryBuffer *m_unifiedGeometryBuffer{nullptr};
    VkDeviceSize m_unifiedVertexBufferSize{0u};
    uint32_t m_unifiedIndexBufferCount{0u};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_MESH_GEOMETRY_REGISTRY_HPP
