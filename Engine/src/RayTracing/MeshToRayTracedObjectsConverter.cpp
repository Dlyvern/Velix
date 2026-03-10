#include "Engine/RayTracing/MeshToRayTracedObjectsConverter.hpp"

#include "Core/VulkanContext.hpp"

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
        return vkGetBufferDeviceAddress(context->getDevice()->vk(), &addressInfo);
    }
} // namespace

bool MeshToRayTracedObjectsConverter::convert(const GPUMesh &mesh,
                                              const RayTracedMesh &rayTracedMesh,
                                              VkAccelerationStructureGeometryKHR &geometry,
                                              VkAccelerationStructureBuildRangeInfoKHR &rangeInfo)
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
    triangles.vertexData.deviceAddress =
        getBufferDeviceAddress(*rayTracedMesh.vertexBuffer) + rayTracedMesh.positionOffset;
    triangles.vertexStride = mesh.vertexStride;
    triangles.maxVertex = vertexCount - 1u;
    triangles.indexType = mesh.indexType;
    triangles.indexData.deviceAddress = getBufferDeviceAddress(*rayTracedMesh.indexBuffer);
    triangles.transformData.deviceAddress = 0u;

    if (triangles.vertexData.deviceAddress == 0u || triangles.indexData.deviceAddress == 0u)
        return false;

    geometry = VkAccelerationStructureGeometryKHR{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.geometry.triangles = triangles;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    rangeInfo = VkAccelerationStructureBuildRangeInfoKHR{};
    rangeInfo.primitiveCount = triangleCount;
    rangeInfo.primitiveOffset = 0u;
    rangeInfo.firstVertex = 0u;
    rangeInfo.transformOffset = 0u;

    return true;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
