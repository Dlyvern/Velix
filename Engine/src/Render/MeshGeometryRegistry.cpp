#include "Engine/Render/MeshGeometryRegistry.hpp"

#include "Engine/Vertex.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void MeshGeometryRegistry::setUnifiedGeometryBuffer(UnifiedGeometryBuffer *unifiedGeometryBuffer,
                                                    VkDeviceSize unifiedVertexBufferSize,
                                                    uint32_t unifiedIndexBufferCount)
{
    m_unifiedGeometryBuffer = unifiedGeometryBuffer;
    m_unifiedVertexBufferSize = unifiedVertexBufferSize;
    m_unifiedIndexBufferCount = unifiedIndexBufferCount;
}

const MeshGeometryRegistry::Entry *MeshGeometryRegistry::find(MeshGeometryHash geometryHash) const
{
    const auto it = m_entries.find(geometryHash);
    if (it == m_entries.end())
        return nullptr;

    return &it->second;
}

MeshGeometryRegistry::Entry *MeshGeometryRegistry::find(MeshGeometryHash geometryHash)
{
    const auto it = m_entries.find(geometryHash);
    if (it == m_entries.end())
        return nullptr;

    return &it->second;
}

bool MeshGeometryRegistry::contains(MeshGeometryHash geometryHash) const
{
    return find(geometryHash) != nullptr;
}

GPUMesh::SharedPtr MeshGeometryRegistry::getOrCreateSharedGeometryMesh(const CPUMesh &mesh)
{
    const MeshGeometryInfo &geometryInfo = mesh.getGeometryInfo();

    if (auto entry = find(geometryInfo.hash))
        return entry->sharedMesh;

    auto sharedMesh = GPUMesh::createFromMesh(mesh);
    auto it = m_entries.emplace(geometryInfo.hash, Entry{}).first;
    it->second.geometryHash = geometryInfo.hash;
    it->second.sharedMesh = sharedMesh;

    const uint64_t skinnedVertexLayoutHash = vertex::VertexTraits<vertex::VertexSkinned>::layout().hash;
    if (sharedMesh &&
        m_unifiedGeometryBuffer != nullptr &&
        mesh.vertexLayoutHash != skinnedVertexLayoutHash &&
        !mesh.vertexData.empty() &&
        !mesh.indices.empty())
    {
        if (!m_unifiedGeometryBuffer->isInitialized())
            m_unifiedGeometryBuffer->init(mesh.vertexStride, m_unifiedVertexBufferSize, m_unifiedIndexBufferCount);

        if (m_unifiedGeometryBuffer->getVertexStride() == mesh.vertexStride)
        {
            int32_t outVertexOffset = GPUMesh::INVALID_VERTEX_OFFSET;
            uint32_t outFirstIndex = 0u;
            if (m_unifiedGeometryBuffer->registerMesh(mesh.vertexData.data(),
                                                      static_cast<VkDeviceSize>(mesh.vertexData.size()),
                                                      mesh.indices.data(),
                                                      static_cast<uint32_t>(mesh.indices.size()),
                                                      outVertexOffset, outFirstIndex))
            {
                sharedMesh->unifiedVertexOffset = outVertexOffset;
                sharedMesh->unifiedFirstIndex = outFirstIndex;
                sharedMesh->inUnifiedBuffer = true;
            }
        }
    }

    return sharedMesh;
}

GPUMesh::SharedPtr MeshGeometryRegistry::createDrawMeshInstance(const CPUMesh &mesh)
{
    auto sharedGeometry = getOrCreateSharedGeometryMesh(mesh);
    if (!sharedGeometry)
        return nullptr;

    auto instance = std::make_shared<GPUMesh>();
    instance->indexBuffer = sharedGeometry->indexBuffer;
    instance->vertexBuffer = sharedGeometry->vertexBuffer;
    instance->indicesCount = sharedGeometry->indicesCount;
    instance->indexType = sharedGeometry->indexType;
    instance->vertexStride = sharedGeometry->vertexStride;
    instance->vertexLayoutHash = sharedGeometry->vertexLayoutHash;
    instance->unifiedVertexOffset = sharedGeometry->unifiedVertexOffset;
    instance->unifiedFirstIndex = sharedGeometry->unifiedFirstIndex;
    instance->inUnifiedBuffer = sharedGeometry->inUnifiedBuffer;

    return instance;
}

void MeshGeometryRegistry::clear()
{
    m_entries.clear();
}

std::size_t MeshGeometryRegistry::size() const
{
    return m_entries.size();
}

ELIX_NESTED_NAMESPACE_END
