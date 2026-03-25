#include "Engine/Render/UnifiedGeometryBuffer.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/Logger.hpp"
#include "Engine/Utilities/BufferUtilities.hpp"
#include "Engine/Utilities/AsyncGpuUpload.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void UnifiedGeometryBuffer::init(uint32_t vertexStride, VkDeviceSize maxVertexBytes, uint32_t maxIndices)
{
    if (vertexStride == 0 || maxVertexBytes == 0 || maxIndices == 0)
    {
        VX_ENGINE_ERROR_STREAM("UnifiedGeometryBuffer::init – invalid parameters");
        return;
    }

    m_vertexStride = vertexStride;
    m_maxVertexBytes = maxVertexBytes;
    m_maxIndices = maxIndices;
    m_vertexBytesUsed = 0;
    m_indicesUsed = 0;

    constexpr VkBufferUsageFlags vertexUsage =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; // for future compute culling

    constexpr VkBufferUsageFlags indexUsage =
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    m_vertexBuffer = core::Buffer::createShared(maxVertexBytes, vertexUsage, core::memory::MemoryUsage::GPU_ONLY);
    m_indexBuffer = core::Buffer::createShared(static_cast<VkDeviceSize>(maxIndices) * sizeof(uint32_t), indexUsage, core::memory::MemoryUsage::GPU_ONLY);

    if (!m_vertexBuffer || !m_indexBuffer)
    {
        VX_ENGINE_ERROR_STREAM("UnifiedGeometryBuffer::init – failed to allocate GPU buffers");
        m_vertexBuffer = nullptr;
        m_indexBuffer = nullptr;
    }
}

bool UnifiedGeometryBuffer::registerMesh(const uint8_t *vertexData, VkDeviceSize vertexBytes,
                                         const uint32_t *indexData, uint32_t indexCount,
                                         int32_t &outVertexOffset, uint32_t &outFirstIndex)
{
    if (!m_vertexBuffer || !m_indexBuffer)
        return false;
    if (!vertexData || vertexBytes == 0 || !indexData || indexCount == 0)
        return false;
    if (vertexBytes % m_vertexStride != 0)
        return false; // stride mismatch

    const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(indexCount) * sizeof(uint32_t);

    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_vertexBytesUsed + vertexBytes > m_maxVertexBytes)
    {
        VX_ENGINE_WARNING_STREAM("UnifiedGeometryBuffer: vertex buffer full ("
                                 << m_vertexBytesUsed << "/" << m_maxVertexBytes << " bytes used)");
        return false;
    }
    if (m_indicesUsed + indexCount > m_maxIndices)
    {
        VX_ENGINE_WARNING_STREAM("UnifiedGeometryBuffer: index buffer full ("
                                 << m_indicesUsed << "/" << m_maxIndices << " indices used)");
        return false;
    }

    const VkDeviceSize vertexDstOffset = m_vertexBytesUsed;
    const uint32_t firstIndex = m_indicesUsed;
    const int32_t vertexOffset = static_cast<int32_t>(m_vertexBytesUsed / m_vertexStride);

    m_vertexBytesUsed += vertexBytes;
    m_indicesUsed += indexCount;

    lock.unlock();

    // Upload vertex data via staging buffer
    {
        auto stagingVB = core::Buffer::createShared(vertexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, core::memory::MemoryUsage::CPU_TO_GPU);
        stagingVB->upload(vertexData, vertexBytes);

        auto cmd = core::CommandBuffer::createShared(*core::VulkanContext::getContext()->getGraphicsCommandPool());
        cmd->begin();
        utilities::BufferUtilities::copyBufferRegion(*stagingVB, *m_vertexBuffer, *cmd,
                                                     vertexBytes, 0, vertexDstOffset);
        cmd->end();

        utilities::AsyncGpuUpload::submit(cmd, core::VulkanContext::getContext()->getGraphicsQueue(), {stagingVB});
    }

    // Upload index data via staging buffer
    {
        auto stagingIB = core::Buffer::createShared(indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, core::memory::MemoryUsage::CPU_TO_GPU);
        stagingIB->upload(indexData, indexBytes);

        auto cmd = core::CommandBuffer::createShared(*core::VulkanContext::getContext()->getGraphicsCommandPool());
        cmd->begin();
        utilities::BufferUtilities::copyBufferRegion(*stagingIB, *m_indexBuffer, *cmd,
                                                     indexBytes, 0,
                                                     static_cast<VkDeviceSize>(firstIndex) * sizeof(uint32_t));
        cmd->end();

        utilities::AsyncGpuUpload::submit(cmd, core::VulkanContext::getContext()->getGraphicsQueue(), {stagingIB});
    }

    outVertexOffset = vertexOffset;
    outFirstIndex = firstIndex;
    return true;
}

ELIX_NESTED_NAMESPACE_END
