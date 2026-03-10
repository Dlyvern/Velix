#include "Engine/RayTracing/RayTracingGeometryCache.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/RayTracing/MeshToRayTracedObjectsConverter.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(rayTracing)

namespace
{
    VkDeviceAddress getBufferDeviceAddress(const core::Buffer &buffer)
    {
        auto context = core::VulkanContext::getContext();
        if (!context || !context->hasBufferDeviceAddressSupport())
            return 0u;

        VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        addressInfo.buffer = buffer.vk();
        return vkGetBufferDeviceAddress(context->getDevice(), &addressInfo);
    }

    bool submitAndWait(core::CommandBuffer::SharedPtr commandBuffer, VkQueue queue)
    {
        if (!commandBuffer || queue == VK_NULL_HANDLE)
            return false;

        auto context = core::VulkanContext::getContext();
        if (!context)
            return false;

        VkFenceCreateInfo fenceCreateInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(context->getDevice(), &fenceCreateInfo, nullptr, &fence) != VK_SUCCESS)
            return false;

        const bool submitted = commandBuffer->submit(queue, {}, {}, {}, fence);
        const bool waited = submitted && vkWaitForFences(context->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;

        vkDestroyFence(context->getDevice(), fence, nullptr);
        return submitted && waited;
    }
} // namespace

const RayTracingGeometryCache::Entry *RayTracingGeometryCache::find(std::size_t geometryHash) const
{
    const auto it = m_entries.find(geometryHash);
    if (it == m_entries.end())
        return nullptr;

    return &it->second;
}

RayTracingGeometryCache::Entry *RayTracingGeometryCache::find(std::size_t geometryHash)
{
    const auto it = m_entries.find(geometryHash);
    if (it == m_entries.end())
        return nullptr;

    return &it->second;
}

bool RayTracingGeometryCache::contains(std::size_t geometryHash) const
{
    return m_entries.find(geometryHash) != m_entries.end();
}

RayTracedMesh::SharedPtr RayTracingGeometryCache::getOrCreate(std::size_t geometryHash,
                                                              const GPUMesh &mesh,
                                                              core::CommandPool::SharedPtr commandPool)
{
    auto it = m_entries.find(geometryHash);
    if (it != m_entries.end() && it->second.rayTracedMesh && !it->second.dirty)
        return it->second.rayTracedMesh;

    auto rayTracedMesh = RayTracedMesh::createFromMesh(mesh, commandPool);
    if (!rayTracedMesh)
        return nullptr;

    if (it == m_entries.end())
        it = m_entries.emplace(geometryHash, Entry{}).first;

    it->second.geometryHash = geometryHash;
    it->second.rayTracedMesh = std::move(rayTracedMesh);
    it->second.dirty = false;
    ++it->second.version;

    return it->second.rayTracedMesh;
}

AccelerationStructure::SharedPtr RayTracingGeometryCache::getOrCreateBLAS(std::size_t geometryHash,
                                                                          const GPUMesh &mesh,
                                                                          core::CommandPool::SharedPtr commandPool)
{
    auto context = core::VulkanContext::getContext();
    if (!context || !context->hasAccelerationStructureSupport())
        return nullptr;

    auto &entry = m_entries[geometryHash];
    entry.geometryHash = geometryHash;

    if (entry.blas && !entry.dirty)
        return entry.blas;

    auto rayTracedMesh = getOrCreate(geometryHash, mesh, commandPool);
    if (!rayTracedMesh)
        return nullptr;

    VkAccelerationStructureGeometryKHR geometry{};
    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    if (!MeshToRayTracedObjectsConverter::convert(mesh, *rayTracedMesh, geometry, rangeInfo))
        return nullptr;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1u;
    buildInfo.pGeometries = &geometry;

    const uint32_t primitiveCount = rangeInfo.primitiveCount;
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(
        context->getDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizeInfo);

    if (!entry.blas || entry.blas->size() < sizeInfo.accelerationStructureSize)
        entry.blas = AccelerationStructure::create(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizeInfo.accelerationStructureSize);

    if (!entry.blas || !entry.blas->isValid())
        return nullptr;

    auto scratchBuffer = core::Buffer::createShared(
        sizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        core::memory::MemoryUsage::GPU_ONLY);

    const VkDeviceAddress scratchAddress = scratchBuffer ? getBufferDeviceAddress(*scratchBuffer) : 0u;
    if (scratchAddress == 0u)
        return nullptr;

    buildInfo.dstAccelerationStructure = entry.blas->vk();
    buildInfo.scratchData.deviceAddress = scratchAddress;

    auto pool = commandPool ? commandPool : context->getGraphicsCommandPool();
    if (!pool)
        return nullptr;

    auto commandBuffer = core::CommandBuffer::createShared(*pool);
    if (!commandBuffer->begin())
        return nullptr;

    const VkAccelerationStructureBuildRangeInfoKHR *buildRanges[] = {&rangeInfo};
    vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1u, &buildInfo, buildRanges);

    if (!commandBuffer->end())
        return nullptr;

    if (!submitAndWait(commandBuffer, context->getGraphicsQueue()))
        return nullptr;

    entry.primitiveCount = primitiveCount;
    entry.dirty = false;
    return entry.blas;
}

void RayTracingGeometryCache::markDirty(std::size_t geometryHash)
{
    auto it = m_entries.find(geometryHash);
    if (it == m_entries.end())
        return;

    it->second.dirty = true;
    it->second.blas.reset();
}

void RayTracingGeometryCache::erase(std::size_t geometryHash)
{
    m_entries.erase(geometryHash);
}

void RayTracingGeometryCache::clear()
{
    m_entries.clear();
}

std::size_t RayTracingGeometryCache::size() const
{
    return m_entries.size();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
