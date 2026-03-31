#include "Engine/RayTracing/RayTracedMesh.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Utilities/AsyncGpuUpload.hpp"
#include "Engine/Utilities/BufferUtilities.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(rayTracing)

RayTracedMesh::SharedPtr RayTracedMesh::createFromMesh(const GPUMesh &mesh,
                                                       core::CommandPool::SharedPtr commandPool,
                                                       UploadSubmission *outPendingUpload)
{
    auto rayTracedMesh = std::make_shared<RayTracedMesh>();

    if (!rayTracedMesh->uploadFromMesh(mesh, commandPool, outPendingUpload))
        return nullptr;

    return rayTracedMesh;
}

bool RayTracedMesh::uploadFromMesh(const GPUMesh &mesh,
                                   core::CommandPool::SharedPtr commandPool,
                                   UploadSubmission *outPendingUpload)
{
    auto context = core::VulkanContext::getContext();

    if (!context || !context->hasBufferDeviceAddressSupport() || !context->hasAccelerationStructureSupport())
        return false;

    if (vertexBuffer && indexBuffer)
        return true;

    if (!mesh.vertexBuffer || !mesh.indexBuffer)
        return false;

    auto pool = commandPool ? commandPool : context->getGraphicsCommandPool();
    if (!pool)
        return false;

    auto commandBuffer = core::CommandBuffer::createShared(*pool);
    if (!commandBuffer->begin())
        return false;

    auto newVertexBuffer = core::Buffer::createShared(
        mesh.vertexBuffer->getSize(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        core::memory::MemoryUsage::GPU_ONLY);

    auto newIndexBuffer = core::Buffer::createShared(
        mesh.indexBuffer->getSize(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        core::memory::MemoryUsage::GPU_ONLY);

    utilities::BufferUtilities::copyBuffer(*mesh.vertexBuffer, *newVertexBuffer, *commandBuffer, mesh.vertexBuffer->getSize());
    utilities::BufferUtilities::copyBuffer(*mesh.indexBuffer, *newIndexBuffer, *commandBuffer, mesh.indexBuffer->getSize());

    if (!commandBuffer->end())
        return false;

    vertexBuffer = std::move(newVertexBuffer);
    indexBuffer = std::move(newIndexBuffer);

    if (outPendingUpload)
    {
        outPendingUpload->fence = utilities::AsyncGpuUpload::submitAsync(commandBuffer, context->getGraphicsQueue());
        if (outPendingUpload->fence == VK_NULL_HANDLE)
            return false;

        outPendingUpload->commandBuffer = std::move(commandBuffer);
        return true;
    }

    if (!utilities::AsyncGpuUpload::submitAndWait(commandBuffer, context->getGraphicsQueue()))
        return false;

    return true;
}

bool RayTracedMesh::fillGeometryDesc(const GPUMesh &mesh,
                                     const RayTracedMesh &rayTracedMesh,
                                     core::rtx::TriangleGeometryDesc &geometryDesc)
{
    if (!rayTracedMesh.vertexBuffer || !rayTracedMesh.indexBuffer || mesh.vertexStride == 0u)
        return false;

    const uint32_t triangleCount = mesh.indicesCount / 3u;
    const uint32_t vertexCount = static_cast<uint32_t>(rayTracedMesh.vertexBuffer->getSize() / mesh.vertexStride);
    if (triangleCount == 0u || vertexCount == 0u)
        return false;

    VkAccelerationStructureGeometryTrianglesDataKHR triangles{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
    triangles.vertexFormat = rayTracedMesh.positionFormat;
    triangles.vertexData.deviceAddress = utilities::BufferUtilities::getBufferDeviceAddress(*rayTracedMesh.vertexBuffer) + rayTracedMesh.positionOffset;
    triangles.vertexStride = mesh.vertexStride;
    triangles.maxVertex = vertexCount - 1u;
    triangles.indexType = mesh.indexType;
    triangles.indexData.deviceAddress = utilities::BufferUtilities::getBufferDeviceAddress(*rayTracedMesh.indexBuffer);
    triangles.transformData.deviceAddress = 0u;

    if (triangles.vertexData.deviceAddress == 0u || triangles.indexData.deviceAddress == 0u)
        return false;

    geometryDesc.vertexAddress = triangles.vertexData.deviceAddress;
    geometryDesc.indexAddress = triangles.indexData.deviceAddress;
    geometryDesc.vertexStride = triangles.vertexStride;
    geometryDesc.vertexCount = vertexCount;
    geometryDesc.triangleCount = triangleCount;
    geometryDesc.vertexFormat = triangles.vertexFormat;
    geometryDesc.indexType = triangles.indexType;
    // Leave geometry flags at 0 so TLAS instance flags still control opacity behavior.
    geometryDesc.geometryFlags = 0u;

    return true;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
