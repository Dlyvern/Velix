#ifndef ELIX_UNIFIED_GEOMETRY_BUFFER_HPP
#define ELIX_UNIFIED_GEOMETRY_BUFFER_HPP

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"

#include <cstdint>
#include <mutex>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

// Append-only, pre-allocated unified vertex + index buffer for static meshes.
// All registered meshes must share the same vertex stride.
// Falls back gracefully (returns false) when the buffer is full or stride mismatches.
class UnifiedGeometryBuffer
{
public:
    // Value stored in GPUMesh::unifiedVertexOffset when NOT registered.
    static constexpr int32_t INVALID_VERTEX_OFFSET = INT32_MIN;

    // Allocate GPU-only vertex and index buffers.
    // vertexStride: bytes per vertex (fixed for all meshes in this buffer).
    // maxVertexBytes: total vertex buffer capacity in bytes.
    // maxIndices: total index buffer capacity (uint32 indices).
    void init(uint32_t vertexStride, VkDeviceSize maxVertexBytes, uint32_t maxIndices);

    // Register mesh geometry into the unified buffer.
    // Uploads data asynchronously (safe to call before the first frame).
    // Returns true on success and fills outVertexOffset / outFirstIndex.
    // Returns false if the buffer is full, not initialised, or stride mismatches.
    bool registerMesh(const uint8_t *vertexData, VkDeviceSize vertexBytes,
                      const uint32_t *indexData, uint32_t indexCount,
                      int32_t &outVertexOffset, uint32_t &outFirstIndex);

    VkBuffer getVertexBuffer() const { return m_vertexBuffer ? m_vertexBuffer->vk() : VK_NULL_HANDLE; }
    VkBuffer getIndexBuffer() const { return m_indexBuffer ? m_indexBuffer->vk() : VK_NULL_HANDLE; }

    uint32_t getVertexStride() const { return m_vertexStride; }
    bool isInitialized() const { return m_vertexBuffer != nullptr; }

    // Returns total capacity in vertices / indices.
    uint32_t vertexCapacity() const { return m_vertexStride > 0 ? static_cast<uint32_t>(m_maxVertexBytes / m_vertexStride) : 0u; }
    uint32_t indexCapacity() const { return m_maxIndices; }
    uint32_t verticesUsed() const { return m_vertexStride > 0 ? static_cast<uint32_t>(m_vertexBytesUsed / m_vertexStride) : 0u; }
    uint32_t indicesUsed() const { return m_indicesUsed; }

private:
    core::Buffer::SharedPtr m_vertexBuffer{nullptr};
    core::Buffer::SharedPtr m_indexBuffer{nullptr};

    uint32_t m_vertexStride{0};
    VkDeviceSize m_vertexBytesUsed{0};
    VkDeviceSize m_maxVertexBytes{0};
    uint32_t m_indicesUsed{0};
    uint32_t m_maxIndices{0};

    std::mutex m_mutex;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_UNIFIED_GEOMETRY_BUFFER_HPP
