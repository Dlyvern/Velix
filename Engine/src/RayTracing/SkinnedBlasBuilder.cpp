#include "Engine/RayTracing/SkinnedBlasBuilder.hpp"

#include "Core/RTX/AccelerationStructureBuilder.hpp"
#include "Core/VulkanContext.hpp"
#include "Engine/Utilities/AsyncGpuUpload.hpp"
#include "Engine/Utilities/BufferUtilities.hpp"
#include "Engine/Vertex.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(rayTracing)

namespace
{
    void destroyPendingBuild(SkinnedBlasBuilder::Entry &entry, bool wait)
    {
        if (entry.pendingBuildFence != VK_NULL_HANDLE)
        {
            auto context = core::VulkanContext::getContext();
            if (context)
            {
                if (wait)
                    vkWaitForFences(context->getDevice(), 1, &entry.pendingBuildFence, VK_TRUE, UINT64_MAX);
                vkDestroyFence(context->getDevice(), entry.pendingBuildFence, nullptr);
            }
        }

        entry.pendingBuildFence = VK_NULL_HANDLE;
        entry.pendingBuildCommandBuffer.reset();
        entry.pendingScratchBuffer.reset();
        entry.buildPending = false;
    }
} // namespace

core::rtx::AccelerationStructure::SharedPtr SkinnedBlasBuilder::buildOrUpdate(
    uint64_t key,
    const CPUMesh &cpuMesh,
    const std::vector<glm::mat4> &boneMatrices,
    uint32_t bonesOffset,
    core::CommandPool::SharedPtr commandPool)
{
    auto context = core::VulkanContext::getContext();
    if (!context || !context->hasAccelerationStructureSupport() || !context->hasBufferDeviceAddressSupport())
        return nullptr;

    if (cpuMesh.vertexData.empty() || cpuMesh.indices.empty() || cpuMesh.vertexStride == 0)
        return nullptr;

    if (cpuMesh.vertexStride != sizeof(vertex::VertexSkinned))
        return nullptr;

    const uint32_t vertexCount = static_cast<uint32_t>(cpuMesh.vertexData.size() / cpuMesh.vertexStride);
    const uint32_t triangleCount = static_cast<uint32_t>(cpuMesh.indices.size() / 3u);
    if (vertexCount == 0u || triangleCount == 0u)
        return nullptr;

    auto &entry = m_entries[key];

    if (entry.buildPending && entry.pendingBuildFence != VK_NULL_HANDLE)
    {
        const VkResult status = vkGetFenceStatus(context->getDevice(), entry.pendingBuildFence);
        if (status == VK_NOT_READY)
            return nullptr;

        destroyPendingBuild(entry, false);
        if (status != VK_SUCCESS)
        {
            entry.blas.reset();
            entry.firstBuild = true;
            return nullptr;
        }

        entry.firstBuild = false;
        return entry.blas;
    }

    const VkDeviceSize vertexBufferSize = static_cast<VkDeviceSize>(vertexCount * sizeof(vertex::Vertex3D));
    const VkDeviceSize indexBufferSize = static_cast<VkDeviceSize>(cpuMesh.indices.size() * sizeof(uint32_t));

    if (!entry.vertexBuffer || entry.vertexBuffer->getSize() < vertexBufferSize)
    {
        entry.vertexBuffer = core::Buffer::createShared(
            vertexBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU);
        entry.firstBuild = true;
    }

    if (!entry.indexBuffer)
    {
        entry.indexBuffer = core::Buffer::createShared(
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU);

        if (!entry.indexBuffer)
        {
            m_entries.erase(key);
            return nullptr;
        }

        void *indexMapped = nullptr;
        entry.indexBuffer->map(indexMapped);
        std::memcpy(indexMapped, cpuMesh.indices.data(), indexBufferSize);
        entry.indexBuffer->unmap();

        entry.firstBuild = true;
    }

    if (!entry.vertexBuffer)
    {
        m_entries.erase(key);
        return nullptr;
    }

    const auto *skinnedVerts = reinterpret_cast<const vertex::VertexSkinned *>(cpuMesh.vertexData.data());

    void *vertMapped = nullptr;
    entry.vertexBuffer->map(vertMapped);
    auto *outVerts = static_cast<vertex::Vertex3D *>(vertMapped);

    for (uint32_t i = 0u; i < vertexCount; ++i)
    {
        const vertex::VertexSkinned &vin = skinnedVerts[i];

        glm::mat4 boneTransform(0.0f);
        bool hasInfluence = false;
        for (int b = 0; b < 4; ++b)
        {
            const int32_t boneId = vin.boneIds[b];
            const float weight = vin.weights[b];
            const uint32_t globalBone = static_cast<uint32_t>(bonesOffset) + static_cast<uint32_t>(boneId);
            if (boneId >= 0 && weight > 0.0f && globalBone < static_cast<uint32_t>(boneMatrices.size()))
            {
                boneTransform += boneMatrices[globalBone] * weight;
                hasInfluence = true;
            }
        }
        if (!hasInfluence)
            boneTransform = glm::mat4(1.0f);

        const glm::mat3 normalMat = glm::mat3(boneTransform);

        vertex::Vertex3D &vout = outVerts[i];
        vout.position = glm::vec3(boneTransform * glm::vec4(vin.position, 1.0f));
        vout.textureCoordinates = vin.textureCoordinates;
        vout.normal = glm::normalize(normalMat * vin.normal);
        vout.tangent = glm::normalize(normalMat * vin.tangent);
        vout.bitangent = glm::normalize(normalMat * vin.bitangent);
    }

    entry.vertexBuffer->unmap();

    const VkDeviceAddress vertexAddr = utilities::BufferUtilities::getBufferDeviceAddress(*entry.vertexBuffer);
    const VkDeviceAddress indexAddr = utilities::BufferUtilities::getBufferDeviceAddress(*entry.indexBuffer);
    if (vertexAddr == 0u || indexAddr == 0u)
        return nullptr;

    core::rtx::TriangleGeometryDesc geometryDesc{};
    geometryDesc.vertexAddress = vertexAddr;
    geometryDesc.indexAddress = indexAddr;
    geometryDesc.vertexStride = sizeof(vertex::Vertex3D);
    geometryDesc.vertexCount = vertexCount;
    geometryDesc.triangleCount = triangleCount;
    geometryDesc.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geometryDesc.indexType = VK_INDEX_TYPE_UINT32;

    if (entry.firstBuild)
    {
        const core::rtx::BuildSizes sizeInfo =
            core::rtx::AccelerationStructureBuilder::queryBottomLevelSizes(
                geometryDesc,
                VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
                    VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);

        entry.blas = core::rtx::AccelerationStructure::create(
            VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            sizeInfo.accelerationStructureSize);

        if (!entry.blas || !entry.blas->isValid())
        {
            m_entries.erase(key);
            return nullptr;
        }

        entry.buildScratchSize = sizeInfo.buildScratchSize;
        entry.updateScratchSize = sizeInfo.updateScratchSize;
        entry.vertexCount = vertexCount;
        entry.triangleCount = triangleCount;
    }

    if (!entry.blas)
        return nullptr;

    // ----- Build / update -----
    const bool isUpdate = !entry.firstBuild;
    const VkDeviceSize scratchSize = isUpdate ? entry.updateScratchSize : entry.buildScratchSize;

    auto scratchBuffer = core::Buffer::createShared(
        scratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        core::memory::MemoryUsage::GPU_ONLY);

    const VkDeviceAddress scratchAddr = scratchBuffer
                                            ? utilities::BufferUtilities::getBufferDeviceAddress(*scratchBuffer)
                                            : 0u;
    if (scratchAddr == 0u)
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
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
            VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
        isUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                 : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        isUpdate ? entry.blas->vk() : VK_NULL_HANDLE);

    if (!commandBuffer->end())
        return nullptr;

    if (entry.firstBuild)
    {
        VkFence fence = utilities::AsyncGpuUpload::submitAsync(commandBuffer, context->getGraphicsQueue());
        if (fence == VK_NULL_HANDLE)
            return nullptr;

        entry.pendingBuildFence = fence;
        entry.pendingBuildCommandBuffer = std::move(commandBuffer);
        entry.pendingScratchBuffer = std::move(scratchBuffer);
        entry.buildPending = true;
        return nullptr;
    }

    if (!utilities::AsyncGpuUpload::submitAndWait(commandBuffer, context->getGraphicsQueue()))
        return nullptr;

    entry.firstBuild = false;
    return entry.blas;
}

const SkinnedBlasBuilder::Entry *SkinnedBlasBuilder::find(uint64_t key) const
{
    const auto it = m_entries.find(key);
    return it == m_entries.end() ? nullptr : &it->second;
}

void SkinnedBlasBuilder::clear()
{
    for (auto &[key, entry] : m_entries)
        destroyPendingBuild(entry, true);
    m_entries.clear();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
