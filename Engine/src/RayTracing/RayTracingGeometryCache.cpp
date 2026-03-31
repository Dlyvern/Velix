#include "Engine/RayTracing/RayTracingGeometryCache.hpp"

#include "Core/RTX/AccelerationStructureBuilder.hpp"
#include "Core/VulkanContext.hpp"
#include "Engine/Utilities/AsyncGpuUpload.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(rayTracing)

namespace
{
    void destroyPendingMeshUpload(RayTracingGeometryCache::Entry &entry, bool wait)
    {
        if (entry.pendingMeshUploadFence != VK_NULL_HANDLE)
        {
            auto context = core::VulkanContext::getContext();
            if (context)
            {
                if (wait)
                    vkWaitForFences(context->getDevice(), 1, &entry.pendingMeshUploadFence, VK_TRUE, UINT64_MAX);
                vkDestroyFence(context->getDevice(), entry.pendingMeshUploadFence, nullptr);
            }
        }

        entry.pendingMeshUploadFence = VK_NULL_HANDLE;
        entry.pendingMeshUploadCommandBuffer.reset();
        entry.meshUploadPending = false;
    }

    void destroyPendingBlasBuild(RayTracingGeometryCache::Entry &entry, bool wait)
    {
        if (entry.pendingBlasFence != VK_NULL_HANDLE)
        {
            auto context = core::VulkanContext::getContext();
            if (context)
            {
                if (wait)
                    vkWaitForFences(context->getDevice(), 1, &entry.pendingBlasFence, VK_TRUE, UINT64_MAX);
                vkDestroyFence(context->getDevice(), entry.pendingBlasFence, nullptr);
            }
        }

        entry.pendingBlasFence = VK_NULL_HANDLE;
        entry.pendingBlasCommandBuffer.reset();
        entry.pendingScratchBuffer.reset();
        entry.blasPending = false;
    }
} // namespace

const RayTracingGeometryCache::Entry *RayTracingGeometryCache::find(MeshGeometryHash geometryHash) const
{
    const auto it = m_entries.find(geometryHash);
    if (it == m_entries.end())
        return nullptr;

    return &it->second;
}

RayTracingGeometryCache::Entry *RayTracingGeometryCache::find(MeshGeometryHash geometryHash)
{
    const auto it = m_entries.find(geometryHash);
    if (it == m_entries.end())
        return nullptr;

    return &it->second;
}

bool RayTracingGeometryCache::contains(MeshGeometryHash geometryHash) const
{
    return find(geometryHash) != nullptr;
}

RayTracedMesh::SharedPtr RayTracingGeometryCache::getOrCreate(MeshGeometryHash geometryHash,
                                                              const GPUMesh &mesh,
                                                              core::CommandPool::SharedPtr commandPool)
{
    auto context = core::VulkanContext::getContext();
    auto &entry = m_entries[geometryHash];
    entry.geometryHash = geometryHash;

    if (entry.meshUploadPending && entry.pendingMeshUploadFence != VK_NULL_HANDLE)
    {
        const VkResult status = context ? vkGetFenceStatus(context->getDevice(), entry.pendingMeshUploadFence) : VK_ERROR_DEVICE_LOST;
        if (status == VK_NOT_READY)
            return nullptr;

        destroyPendingMeshUpload(entry, false);
        if (status != VK_SUCCESS)
        {
            entry.rayTracedMesh.reset();
            return nullptr;
        }

        if (entry.rayTracedMesh && !entry.dirty)
            return entry.rayTracedMesh;
    }

    if (entry.rayTracedMesh && !entry.dirty)
        return entry.rayTracedMesh;

    RayTracedMesh::UploadSubmission pendingUpload{};
    auto rayTracedMesh = RayTracedMesh::createFromMesh(mesh, commandPool, &pendingUpload);

    if (!rayTracedMesh)
        return nullptr;

    entry.rayTracedMesh = std::move(rayTracedMesh);
    entry.dirty = false;
    ++entry.version;

    if (pendingUpload.fence != VK_NULL_HANDLE)
    {
        entry.pendingMeshUploadFence = pendingUpload.fence;
        entry.pendingMeshUploadCommandBuffer = std::move(pendingUpload.commandBuffer);
        entry.meshUploadPending = true;
        return nullptr;
    }

    return entry.rayTracedMesh;
}

core::rtx::AccelerationStructure::SharedPtr RayTracingGeometryCache::getOrCreateBLAS(
    MeshGeometryHash geometryHash,
    const GPUMesh &mesh,
    core::CommandPool::SharedPtr commandPool)
{
    auto context = core::VulkanContext::getContext();
    if (!context || !context->hasAccelerationStructureSupport())
        return nullptr;

    auto &entry = m_entries[geometryHash];
    entry.geometryHash = geometryHash;

    // Poll an in-flight async BLAS build.
    if (entry.blasPending && entry.pendingBlasFence != VK_NULL_HANDLE)
    {
        const VkResult status = vkGetFenceStatus(context->getDevice(), entry.pendingBlasFence);
        if (status == VK_NOT_READY)
            return nullptr; // Still building — skip this instance for this frame.

        // Build complete (VK_SUCCESS) or failed — clean up regardless.
        destroyPendingBlasBuild(entry, false);

        if (status != VK_SUCCESS)
        {
            entry.blas.reset();
            return nullptr;
        }

        return entry.blas;
    }

    if (entry.blas && !entry.dirty)
        return entry.blas;

    auto rayTracedMesh = getOrCreate(geometryHash, mesh, commandPool);
    if (!rayTracedMesh)
        return nullptr;

    core::rtx::TriangleGeometryDesc geometryDesc{};
    if (!RayTracedMesh::fillGeometryDesc(mesh, *rayTracedMesh, geometryDesc))
        return nullptr;

    const core::rtx::BuildSizes sizeInfo =
        core::rtx::AccelerationStructureBuilder::queryBottomLevelSizes(
            geometryDesc,
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

    if (!entry.blas || entry.blas->size() < sizeInfo.accelerationStructureSize)
        entry.blas = core::rtx::AccelerationStructure::create(
            VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            sizeInfo.accelerationStructureSize);

    if (!entry.blas || !entry.blas->isValid())
        return nullptr;

    auto scratchBuffer = core::Buffer::createShared(
        sizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        core::memory::MemoryUsage::GPU_ONLY);
    if (!scratchBuffer)
        return nullptr;

    auto pool = commandPool ? commandPool : context->getGraphicsCommandPool();
    if (!pool)
        return nullptr;

    auto commandBuffer = core::CommandBuffer::createShared(*pool);
    if (!commandBuffer->begin())
        return nullptr;

    core::rtx::AccelerationStructureBuilder::recordBottomLevelBuild(
        *commandBuffer,
        *entry.blas,
        geometryDesc,
        *scratchBuffer,
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR);

    if (!commandBuffer->end())
        return nullptr;

    // Submit asynchronously — the scratch buffer is kept alive in the Entry until the
    // fence signals (polled on the next call to getOrCreateBLAS for this hash).
    VkFence fence = utilities::AsyncGpuUpload::submitAsync(commandBuffer, context->getGraphicsQueue());
    if (fence == VK_NULL_HANDLE)
        return nullptr;

    entry.pendingBlasFence = fence;
    entry.pendingBlasCommandBuffer = std::move(commandBuffer);
    entry.pendingScratchBuffer = scratchBuffer;
    entry.blasPending = true;
    entry.primitiveCount = geometryDesc.triangleCount;
    entry.dirty = false;
    // Return nullptr this frame; the BLAS will be available next frame once the build completes.
    return nullptr;
}

void RayTracingGeometryCache::markDirty(MeshGeometryHash geometryHash)
{
    auto it = m_entries.find(geometryHash);
    if (it == m_entries.end())
        return;

    destroyPendingMeshUpload(it->second, true);
    destroyPendingBlasBuild(it->second, true);
    it->second.dirty = true;
    it->second.rayTracedMesh.reset();
    it->second.blas.reset();
}

void RayTracingGeometryCache::erase(MeshGeometryHash geometryHash)
{
    auto it = m_entries.find(geometryHash);
    if (it != m_entries.end())
    {
        destroyPendingMeshUpload(it->second, true);
        destroyPendingBlasBuild(it->second, true);
    }
    m_entries.erase(geometryHash);
}

void RayTracingGeometryCache::clear()
{
    for (auto &[hash, entry] : m_entries)
    {
        destroyPendingMeshUpload(entry, true);
        destroyPendingBlasBuild(entry, true);
    }
    m_entries.clear();
}

std::size_t RayTracingGeometryCache::size() const
{
    return m_entries.size();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
