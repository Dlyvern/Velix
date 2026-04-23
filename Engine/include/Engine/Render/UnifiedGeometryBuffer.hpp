#ifndef ELIX_UNIFIED_GEOMETRY_BUFFER_HPP
#define ELIX_UNIFIED_GEOMETRY_BUFFER_HPP

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"

#include <cstdint>
#include <mutex>

ELIX_NESTED_NAMESPACE_BEGIN(engine)




class UnifiedGeometryBuffer
{
public:

    static constexpr int32_t INVALID_VERTEX_OFFSET = INT32_MIN;





    void init(uint32_t vertexStride, VkDeviceSize maxVertexBytes, uint32_t maxIndices);





    bool registerMesh(const uint8_t *vertexData, VkDeviceSize vertexBytes,
                      const uint32_t *indexData, uint32_t indexCount,
                      int32_t &outVertexOffset, uint32_t &outFirstIndex);

    VkBuffer getVertexBuffer() const { return m_vertexBuffer ? m_vertexBuffer->vk() : VK_NULL_HANDLE; }
    VkBuffer getIndexBuffer() const { return m_indexBuffer ? m_indexBuffer->vk() : VK_NULL_HANDLE; }

    uint32_t getVertexStride() const { return m_vertexStride; }
    bool isInitialized() const { return m_vertexBuffer != nullptr; }


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

#endif
