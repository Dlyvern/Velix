#ifndef ELIX_RAY_TRACING_GEOMETRY_CACHE_HPP
#define ELIX_RAY_TRACING_GEOMETRY_CACHE_HPP

#include "Core/Macros.hpp"
#include "Core/RTX/AccelerationStructure.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/RayTracing/RayTracedMesh.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(rayTracing)

class RayTracingGeometryCache
{
public:
    struct Entry
    {
        MeshGeometryHash geometryHash{};
        RayTracedMesh::SharedPtr rayTracedMesh{nullptr};
        core::rtx::AccelerationStructure::SharedPtr blas{nullptr};
        uint32_t primitiveCount{0u};
        uint64_t version{0u};
        bool dirty{false};

        // Async RT mesh upload tracking.
        VkFence pendingMeshUploadFence{VK_NULL_HANDLE};
        core::CommandBuffer::SharedPtr pendingMeshUploadCommandBuffer{nullptr};
        bool meshUploadPending{false};

        // Async BLAS build tracking: fence is non-null while the GPU is still building.
        VkFence pendingBlasFence{VK_NULL_HANDLE};
        core::CommandBuffer::SharedPtr pendingBlasCommandBuffer{nullptr};
        core::Buffer::SharedPtr pendingScratchBuffer{nullptr};
        bool blasPending{false};
    };

    const Entry *find(MeshGeometryHash geometryHash) const;
    Entry *find(MeshGeometryHash geometryHash);

    bool contains(MeshGeometryHash geometryHash) const;

    RayTracedMesh::SharedPtr getOrCreate(MeshGeometryHash geometryHash,
                                         const GPUMesh &mesh,
                                         core::CommandPool::SharedPtr commandPool = nullptr);

    core::rtx::AccelerationStructure::SharedPtr getOrCreateBLAS(MeshGeometryHash geometryHash,
                                                                const GPUMesh &mesh,
                                                                core::CommandPool::SharedPtr commandPool = nullptr);

    void markDirty(MeshGeometryHash geometryHash);
    void erase(MeshGeometryHash geometryHash);
    void clear();

    std::size_t size() const;

private:
    std::unordered_map<MeshGeometryHash, Entry, MeshGeometryHashHasher> m_entries;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RAY_TRACING_GEOMETRY_CACHE_HPP
