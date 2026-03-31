#ifndef ELIX_MESH_HPP
#define ELIX_MESH_HPP

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"
#include "Core/VulkanContext.hpp"

#include "Engine/Caches/Hash.hpp"
#include "Engine/Material.hpp"
#include "Engine/Vertex.hpp"
#include "Engine/Utilities/AsyncGpuUpload.hpp"
#include "Engine/Utilities/BufferUtilities.hpp"

#include "Core/Logger.hpp"

#include <memory>
#include <cstdint>
#include <string>
#include <optional>
#include <limits>

#include <glm/glm.hpp>
#include <cstring>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct MeshGeometryHash
{
    std::size_t value{0u};

    bool isValid() const
    {
        return value != 0u;
    }

    explicit operator bool() const
    {
        return isValid();
    }

    bool operator==(const MeshGeometryHash &other) const = default;
};

struct MeshGeometryHashHasher
{
    std::size_t operator()(const MeshGeometryHash &hash) const noexcept
    {
        return hash.value;
    }
};

struct MeshGeometryInfo
{
    MeshGeometryHash hash{};
    glm::vec3 localBoundsCenter{0.0f};
    float localBoundsRadius{0.0f};
};

struct CPUMesh
{
    std::string name;

    std::vector<uint8_t> vertexData;
    std::vector<uint32_t> indices;

    uint32_t vertexStride;
    uint64_t vertexLayoutHash;
    int32_t attachedBoneId{-1}; // for rigid meshes parented to skeleton bones

    CPUMaterial material;
    glm::mat4 localTransform{1.0f};

    template <typename VertexT>
    static CPUMesh build(const std::vector<VertexT> &vertices, const std::vector<uint32_t> &indices)
    {
        CPUMesh mesh{};
        mesh.vertexStride = sizeof(VertexT);
        mesh.indices = indices;
        // mesh.material = material;

        auto layout = vertex::VertexTraits<VertexT>::layout();
        mesh.vertexLayoutHash = layout.hash;

        mesh.vertexData.resize(vertices.size() * sizeof(VertexT));
        std::memcpy(mesh.vertexData.data(), vertices.data(), mesh.vertexData.size());

        return mesh;
    }

    const MeshGeometryInfo &getGeometryInfo() const
    {
        if (!m_cachedGeometryInfo.has_value())
            m_cachedGeometryInfo = buildGeometryInfo();

        return *m_cachedGeometryInfo;
    }

    void invalidateGeometryInfo()
    {
        m_cachedGeometryInfo.reset();
    }

private:
    MeshGeometryInfo buildGeometryInfo() const
    {
        MeshGeometryInfo info{};

        std::size_t hashData{0u};
        hashing::hash(hashData, vertexStride);
        hashing::hash(hashData, vertexLayoutHash);

        for (const auto &vertexByte : vertexData)
            hashing::hash(hashData, vertexByte);

        for (const auto &index : indices)
            hashing::hash(hashData, index);

        // Keep zero reserved as the "invalid / missing geometry" sentinel.
        if (hashData == 0u)
            hashData = 1u;

        info.hash = MeshGeometryHash{hashData};

        if (vertexStride < sizeof(glm::vec3) || vertexData.empty())
            return info;

        const uint32_t vertexCount = static_cast<uint32_t>(vertexData.size() / vertexStride);
        if (vertexCount == 0u)
            return info;

        glm::vec3 minPosition(std::numeric_limits<float>::max());
        glm::vec3 maxPosition(-std::numeric_limits<float>::max());

        for (uint32_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
        {
            glm::vec3 position(0.0f);
            std::memcpy(&position, vertexData.data() + static_cast<size_t>(vertexIndex) * vertexStride, sizeof(glm::vec3));
            minPosition = glm::min(minPosition, position);
            maxPosition = glm::max(maxPosition, position);
        }

        info.localBoundsCenter = (minPosition + maxPosition) * 0.5f;
        info.localBoundsRadius = glm::length((maxPosition - minPosition) * 0.5f);

        return info;
    }

    mutable std::optional<MeshGeometryInfo> m_cachedGeometryInfo{};
};

struct GPUMesh
{
    using SharedPtr = std::shared_ptr<GPUMesh>;

    core::Buffer::SharedPtr indexBuffer{nullptr};
    core::Buffer::SharedPtr vertexBuffer{nullptr};
    uint32_t indicesCount{0};
    VkIndexType indexType{VK_INDEX_TYPE_UINT32};
    uint32_t vertexStride{0};
    uint64_t vertexLayoutHash{0};

    Material::SharedPtr material{nullptr};

    // Unified geometry buffer registration (set when the mesh is registered
    // in UnifiedGeometryBuffer; INVALID_VERTEX_OFFSET means not registered).
    static constexpr int32_t INVALID_VERTEX_OFFSET = INT32_MIN;
    int32_t unifiedVertexOffset{INVALID_VERTEX_OFFSET}; // vertex index offset in unified VB
    uint32_t unifiedFirstIndex{0};                      // index offset in unified IB
    bool inUnifiedBuffer{false};

    GPUMesh() = default;

    static std::shared_ptr<GPUMesh> create(const std::vector<uint8_t> &vertexData, std::vector<uint32_t> indices, core::CommandPool::SharedPtr commandPool = nullptr)
    {
        if (vertexData.empty() || indices.empty())
        {
            VX_ENGINE_WARNING_STREAM("Skipping GPUMesh creation for empty geometry. vertexBytes=" << vertexData.size()
                                                                                                  << ", indices=" << indices.size() << '\n');
            return nullptr;
        }

        auto gpu = std::make_shared<GPUMesh>();

        VkDeviceSize indexSize = sizeof(indices[0]) * indices.size();
        VkDeviceSize vertexSize = sizeof(vertexData[0]) * vertexData.size();

        auto commandBuffer = core::CommandBuffer::createShared(*core::VulkanContext::getContext()->getGraphicsCommandPool());
        commandBuffer->begin();

        // Vertex
        auto vertexStaging = core::Buffer::createShared(vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, core::memory::MemoryUsage::CPU_TO_GPU);
        vertexStaging->upload(vertexData.data(), vertexSize);

        auto vertexGPUBuffer = core::Buffer::createShared(vertexSize,
                                                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                          core::memory::MemoryUsage::CPU_TO_GPU);

        utilities::BufferUtilities::copyBuffer(*vertexStaging, *vertexGPUBuffer, *commandBuffer, vertexSize);

        // Index
        auto staging = core::Buffer::createShared(indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, core::memory::MemoryUsage::CPU_TO_GPU);
        staging->upload(indices.data(), indexSize);

        auto gpuBuffer = core::Buffer::createShared(indexSize,
                                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                    core::memory::MemoryUsage::CPU_TO_GPU);

        utilities::BufferUtilities::copyBuffer(*staging, *gpuBuffer, *commandBuffer, indexSize);

        commandBuffer->end();

        if (!utilities::AsyncGpuUpload::submit(commandBuffer, core::VulkanContext::getContext()->getGraphicsQueue(), {vertexStaging, staging}))
            return nullptr;

        gpu->vertexBuffer = vertexGPUBuffer;
        gpu->indexBuffer = gpuBuffer;

        gpu->indicesCount = static_cast<uint32_t>(indices.size());

        return gpu;
    }

    static std::shared_ptr<GPUMesh> createFromMesh(const CPUMesh &mesh, core::CommandPool::SharedPtr commandPool = nullptr)
    {
        auto gpuMesh = create(mesh.vertexData, mesh.indices, commandPool);
        if (!gpuMesh)
            return nullptr;

        gpuMesh->vertexStride = mesh.vertexStride;
        gpuMesh->vertexLayoutHash = mesh.vertexLayoutHash;
        return gpuMesh;
    }
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_MESH_HPP
