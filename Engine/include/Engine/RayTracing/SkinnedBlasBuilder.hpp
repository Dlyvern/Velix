#ifndef ELIX_SKINNED_BLAS_BUILDER_HPP
#define ELIX_SKINNED_BLAS_BUILDER_HPP

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"
#include "Core/CommandPool.hpp"
#include "Core/RTX/AccelerationStructure.hpp"
#include "Engine/Mesh.hpp"

#include <glm/mat4x4.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(rayTracing)
class SkinnedBlasBuilder
{
public:
    struct Entry
    {
        core::Buffer::SharedPtr vertexBuffer{nullptr};
        core::Buffer::SharedPtr indexBuffer{nullptr};
        core::rtx::AccelerationStructure::SharedPtr blas{nullptr};
        VkDeviceSize buildScratchSize{0};
        VkDeviceSize updateScratchSize{0};
        uint32_t vertexCount{0};
        uint32_t triangleCount{0};
        bool firstBuild{true};
        VkFence pendingBuildFence{VK_NULL_HANDLE};
        core::CommandBuffer::SharedPtr pendingBuildCommandBuffer{nullptr};
        core::Buffer::SharedPtr pendingScratchBuffer{nullptr};
        bool buildPending{false};
    };

    // key = combined hash of entity ptr + mesh index (caller's responsibility).
    // Returns the ready BLAS, or nullptr on failure.
    core::rtx::AccelerationStructure::SharedPtr buildOrUpdate(
        uint64_t key,
        const CPUMesh &cpuMesh,
        const std::vector<glm::mat4> &boneMatrices,
        uint32_t bonesOffset,
        core::CommandPool::SharedPtr commandPool = nullptr);

    const Entry *find(uint64_t key) const;

    void clear();

private:
    std::unordered_map<uint64_t, Entry> m_entries;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SKINNED_BLAS_BUILDER_HPP
