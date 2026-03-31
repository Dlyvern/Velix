#include "Engine/RayTracing/RayTracingScene.hpp"

#include "Core/RTX/AccelerationStructureBuilder.hpp"
#include "Engine/Caches/Hash.hpp"
#include "Engine/Utilities/BufferUtilities.hpp"
#include "Engine/Utilities/AsyncGpuUpload.hpp"

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
        if (frame.instanceBuffer || frame.tlas)
            utilities::AsyncGpuUpload::flush(context->getDevice());

        frame.instanceBuffer.reset();
        frame.tlas.reset();
        frame.instanceCount = 0u;
        frame.contentHash = 0u;
        return true;
    }

    std::size_t contentHash = 0u;

    for (const auto &instance : instances)
    {
        hashing::hash(contentHash, instance.geometryHash.value);
        hashing::hash(contentHash, instance.customInstanceIndex);
        hashing::hash(contentHash, static_cast<uint32_t>(instance.mask));
        hashing::hash(contentHash, static_cast<uint32_t>(instance.forceOpaque));
        hashing::hash(contentHash, static_cast<uint32_t>(instance.disableTriangleFacingCull));
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                hashing::hash(contentHash, std::hash<float>{}(instance.transform[column][row]));
    }

    //*Frames are the same(Nothing has changed from last frame)
    if (frame.tlas && frame.instanceCount == instances.size() && frame.contentHash == contentHash)
        return true;

    std::vector<core::rtx::InstanceDesc> instanceDescs;
    instanceDescs.reserve(instances.size());

    for (const auto &instance : instances)
    {
        core::rtx::AccelerationStructure::SharedPtr blas;

        if (instance.prebuiltBlas)
        {
            // Skinned mesh: BLAS was already built by SkinnedBlasBuilder.
            blas = instance.prebuiltBlas;
        }
        else
        {
            if (!instance.mesh)
                continue;
            blas = geometryCache.getOrCreateBLAS(instance.geometryHash, *instance.mesh, commandPool);
        }

        if (!blas || !blas->isValid() || blas->deviceAddress() == 0u)
            continue;

        core::rtx::InstanceDesc instanceDesc{};
        instanceDesc.transform = toVkTransformMatrix(instance.transform);
        instanceDesc.customIndex = instance.customInstanceIndex;
        instanceDesc.mask = instance.mask;
        instanceDesc.instanceShaderBindingTableRecordOffset = 0u;
        instanceDesc.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR;
        if (glm::determinant(glm::mat3(instance.transform)) < 0.0f)
            instanceDesc.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR;
        if (instance.forceOpaque)
            instanceDesc.flags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
        if (instance.disableTriangleFacingCull)
            instanceDesc.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instanceDesc.blasDeviceAddress = blas->deviceAddress();
        instanceDescs.push_back(instanceDesc);
    }

    if (instanceDescs.empty())
    {
        if (frame.instanceBuffer || frame.tlas)
            utilities::AsyncGpuUpload::flush(context->getDevice());

        frame.instanceBuffer.reset();
        frame.tlas.reset();
        frame.instanceCount = 0u;
        frame.contentHash = 0u;
        return true;
    }

    std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
    vkInstances.reserve(instanceDescs.size());
    for (const auto &instanceDesc : instanceDescs)
        vkInstances.push_back(core::rtx::AccelerationStructureBuilder::toVkInstance(instanceDesc));

    const VkDeviceSize instanceBufferSize = static_cast<VkDeviceSize>(vkInstances.size() * sizeof(VkAccelerationStructureInstanceKHR));

    frame.instanceBuffer = core::Buffer::createShared(
        instanceBufferSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        core::memory::MemoryUsage::CPU_TO_GPU);

    frame.instanceBuffer->upload(vkInstances.data(), instanceBufferSize);

    const VkDeviceAddress instanceBufferAddress = utilities::BufferUtilities::getBufferDeviceAddress(*frame.instanceBuffer);
    if (instanceBufferAddress == 0u)
        return false;

    const uint32_t primitiveCount = static_cast<uint32_t>(vkInstances.size());
    const core::rtx::BuildSizes sizeInfo =
        core::rtx::AccelerationStructureBuilder::queryTopLevelSizes(
            primitiveCount,
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

    if (!frame.tlas || frame.tlas->size() < sizeInfo.accelerationStructureSize)
        frame.tlas = core::rtx::AccelerationStructure::create(
            VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            sizeInfo.accelerationStructureSize);

    if (!frame.tlas || !frame.tlas->isValid())
        return false;

    auto scratchBuffer = core::Buffer::createShared(
        sizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        core::memory::MemoryUsage::GPU_ONLY);
    if (!scratchBuffer)
        return false;

    auto pool = commandPool ? commandPool : context->getGraphicsCommandPool();
    if (!pool)
        return false;

    auto commandBuffer = core::CommandBuffer::createShared(*pool);
    if (!commandBuffer->begin())
        return false;

    core::rtx::AccelerationStructureBuilder::recordTopLevelBuild(
        *commandBuffer,
        *frame.tlas,
        instanceBufferAddress,
        primitiveCount,
        *scratchBuffer,
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR);

    if (!commandBuffer->end())
        return false;

    // Submit asynchronously. Keep both the scratch buffer and the uploaded instance
    // buffer alive until the upload fence signals; the TLAS build command reads both.
    // The completion semaphore is picked up by RenderGraph::end() and added as a wait
    // semaphore for the frame submit, ensuring the TLAS build is complete before any
    // RT pass reads the TLAS.
    if (!utilities::AsyncGpuUpload::submit(commandBuffer, context->getGraphicsQueue(),
                                           {scratchBuffer, frame.instanceBuffer}))
        return false;

    frame.instanceCount = primitiveCount;
    frame.contentHash = contentHash;
    return true;
}

void RayTracingScene::clear()
{
    if (auto context = core::VulkanContext::getContext())
        utilities::AsyncGpuUpload::flush(context->getDevice());

    for (auto &frame : m_frames)
    {
        frame.instanceBuffer.reset();
        frame.tlas.reset();
        frame.instanceCount = 0u;
        frame.contentHash = 0u;
    }
}

const core::rtx::AccelerationStructure::SharedPtr &RayTracingScene::getTLAS(uint32_t frameIndex) const
{
    static const core::rtx::AccelerationStructure::SharedPtr empty{};

    if (frameIndex >= m_frames.size())
        return empty;

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
