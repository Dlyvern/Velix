#include "Engine/RayTracing/RayTracingScene.hpp"

#include "Engine/Caches/Hash.hpp"
#include "Core/VulkanContext.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(rayTracing)

namespace
{
    VkTransformMatrixKHR toVkTransformMatrix(const glm::mat4 &matrix)
    {
        VkTransformMatrixKHR out{};
        out.matrix[0][0] = matrix[0][0];
        out.matrix[0][1] = matrix[1][0];
        out.matrix[0][2] = matrix[2][0];
        out.matrix[0][3] = matrix[3][0];
        out.matrix[1][0] = matrix[0][1];
        out.matrix[1][1] = matrix[1][1];
        out.matrix[1][2] = matrix[2][1];
        out.matrix[1][3] = matrix[3][1];
        out.matrix[2][0] = matrix[0][2];
        out.matrix[2][1] = matrix[1][2];
        out.matrix[2][2] = matrix[2][2];
        out.matrix[2][3] = matrix[3][2];
        return out;
    }

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

    std::size_t hashInstances(const std::vector<RayTracingScene::InstanceInput> &instances)
    {
        std::size_t seed = 0u;
        for (const auto &instance : instances)
        {
            hashing::hash(seed, instance.geometryHash);
            hashing::hash(seed, instance.customInstanceIndex);
            hashing::hash(seed, static_cast<uint32_t>(instance.mask));
            hashing::hash(seed, static_cast<uint32_t>(instance.forceOpaque));
            hashing::hash(seed, static_cast<uint32_t>(instance.disableTriangleFacingCull));
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    hashing::hash(seed, std::hash<float>{}(instance.transform[column][row]));
        }

        return seed;
    }
} // namespace

RayTracingScene::RayTracingScene(uint32_t framesInFlight)
{
    m_frames.resize(framesInFlight);
}

bool RayTracingScene::update(uint32_t frameIndex,
                             const std::vector<InstanceInput> &instances,
                             RayTracingGeometryCache &geometryCache,
                             core::CommandPool::SharedPtr commandPool)
{
    auto context = core::VulkanContext::getContext();
    if (!context || !context->hasAccelerationStructureSupport() || frameIndex >= m_frames.size())
        return false;

    auto &frame = m_frames[frameIndex];

    if (instances.empty())
    {
        frame.instanceBuffer.reset();
        frame.tlas.reset();
        frame.instanceCount = 0u;
        frame.contentHash = 0u;
        return true;
    }

    const std::size_t contentHash = hashInstances(instances);
    if (frame.tlas && frame.instanceCount == instances.size() && frame.contentHash == contentHash)
        return true;

    std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
    vkInstances.reserve(instances.size());

    for (const auto &instance : instances)
    {
        if (!instance.mesh)
            continue;

        auto blas = geometryCache.getOrCreateBLAS(instance.geometryHash, *instance.mesh, commandPool);
        if (!blas || !blas->isValid() || blas->deviceAddress() == 0u)
            continue;

        VkAccelerationStructureInstanceKHR vkInstance{};
        vkInstance.transform = toVkTransformMatrix(instance.transform);
        vkInstance.instanceCustomIndex = instance.customInstanceIndex;
        vkInstance.mask = instance.mask;
        vkInstance.instanceShaderBindingTableRecordOffset = 0u;
        vkInstance.flags = 0u;
        if (instance.forceOpaque)
            vkInstance.flags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
        if (instance.disableTriangleFacingCull)
            vkInstance.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        vkInstance.accelerationStructureReference = blas->deviceAddress();
        vkInstances.push_back(vkInstance);
    }

    if (vkInstances.empty())
    {
        frame.instanceBuffer.reset();
        frame.tlas.reset();
        frame.instanceCount = 0u;
        frame.contentHash = 0u;
        return true;
    }

    const VkDeviceSize instanceBufferSize =
        static_cast<VkDeviceSize>(vkInstances.size() * sizeof(VkAccelerationStructureInstanceKHR));

    frame.instanceBuffer = core::Buffer::createShared(
        instanceBufferSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        core::memory::MemoryUsage::CPU_TO_GPU);
    frame.instanceBuffer->upload(vkInstances.data(), instanceBufferSize);

    VkAccelerationStructureGeometryInstancesDataKHR instancesData{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
    instancesData.arrayOfPointers = VK_FALSE;
    instancesData.data.deviceAddress = getBufferDeviceAddress(*frame.instanceBuffer);
    if (instancesData.data.deviceAddress == 0u)
        return false;

    VkAccelerationStructureGeometryKHR geometry{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instancesData;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1u;
    buildInfo.pGeometries = &geometry;

    const uint32_t primitiveCount = static_cast<uint32_t>(vkInstances.size());
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(
        context->getDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizeInfo);

    if (!frame.tlas || frame.tlas->size() < sizeInfo.accelerationStructureSize)
        frame.tlas = AccelerationStructure::create(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, sizeInfo.accelerationStructureSize);

    if (!frame.tlas || !frame.tlas->isValid())
        return false;

    auto scratchBuffer = core::Buffer::createShared(
        sizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        core::memory::MemoryUsage::GPU_ONLY);

    const VkDeviceAddress scratchAddress = scratchBuffer ? getBufferDeviceAddress(*scratchBuffer) : 0u;
    if (scratchAddress == 0u)
        return false;

    buildInfo.dstAccelerationStructure = frame.tlas->vk();
    buildInfo.scratchData.deviceAddress = scratchAddress;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR *buildRanges[] = {&rangeInfo};

    auto pool = commandPool ? commandPool : context->getGraphicsCommandPool();
    if (!pool)
        return false;

    auto commandBuffer = core::CommandBuffer::createShared(*pool);
    if (!commandBuffer->begin())
        return false;

    vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1u, &buildInfo, buildRanges);

    if (!commandBuffer->end())
        return false;

    if (!submitAndWait(commandBuffer, context->getGraphicsQueue()))
        return false;

    frame.instanceCount = primitiveCount;
    frame.contentHash = contentHash;
    return true;
}

void RayTracingScene::clear()
{
    for (auto &frame : m_frames)
    {
        frame.instanceBuffer.reset();
        frame.tlas.reset();
        frame.instanceCount = 0u;
        frame.contentHash = 0u;
    }
}

const AccelerationStructure::SharedPtr &RayTracingScene::getTLAS(uint32_t frameIndex) const
{
    static AccelerationStructure::SharedPtr s_null;
    if (frameIndex >= m_frames.size())
        return s_null;

    return m_frames[frameIndex].tlas;
}

uint32_t RayTracingScene::getInstanceCount(uint32_t frameIndex) const
{
    if (frameIndex >= m_frames.size())
        return 0u;

    return m_frames[frameIndex].instanceCount;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
