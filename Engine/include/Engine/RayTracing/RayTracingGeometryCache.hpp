#ifndef ELIX_RAY_TRACING_GEOMETRY_CACHE_HPP
#define ELIX_RAY_TRACING_GEOMETRY_CACHE_HPP

#include "Core/Macros.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/RayTracing/AccelerationStructure.hpp"
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
        std::size_t geometryHash{0u};
        RayTracedMesh::SharedPtr rayTracedMesh{nullptr};
        AccelerationStructure::SharedPtr blas{nullptr};
        uint32_t primitiveCount{0u};
        uint64_t version{0u};
        bool dirty{false};
    };

    const Entry *find(std::size_t geometryHash) const;
    Entry *find(std::size_t geometryHash);

    bool contains(std::size_t geometryHash) const;

    RayTracedMesh::SharedPtr getOrCreate(std::size_t geometryHash,
                                         const GPUMesh &mesh,
                                         core::CommandPool::SharedPtr commandPool = nullptr);

    AccelerationStructure::SharedPtr getOrCreateBLAS(std::size_t geometryHash,
                                                     const GPUMesh &mesh,
                                                     core::CommandPool::SharedPtr commandPool = nullptr);

    void markDirty(std::size_t geometryHash);
    void erase(std::size_t geometryHash);
    void clear();

    std::size_t size() const;

private:
    std::unordered_map<std::size_t, Entry> m_entries;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RAY_TRACING_GEOMETRY_CACHE_HPP
