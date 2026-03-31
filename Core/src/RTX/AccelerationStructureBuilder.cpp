#include "Core/RTX/AccelerationStructureBuilder.hpp"

#include "Core/VulkanContext.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(rtx)

namespace
{
    VkDeviceAddress getBufferDeviceAddress(const core::Buffer &buffer)
    {
        auto context = core::VulkanContext::getContext();
        if (!context)
            return 0u;

        VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        addressInfo.buffer = buffer.vk();
        return vkGetBufferDeviceAddress(context->getDevice(), &addressInfo);
    }

    VkAccelerationStructureGeometryKHR makeTriangleGeometry(const TriangleGeometryDesc &geometryDesc)
    {
        VkAccelerationStructureGeometryTrianglesDataKHR triangles{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
        triangles.vertexFormat = geometryDesc.vertexFormat;
        triangles.vertexData.deviceAddress = geometryDesc.vertexAddress;
        triangles.vertexStride = geometryDesc.vertexStride;
        triangles.maxVertex = geometryDesc.vertexCount > 0u ? geometryDesc.vertexCount - 1u : 0u;
        triangles.indexType = geometryDesc.indexType;
        triangles.indexData.deviceAddress = geometryDesc.indexAddress;
        triangles.transformData.deviceAddress = 0u;

        VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.geometry.triangles = triangles;
        geometry.flags = geometryDesc.geometryFlags;
        return geometry;
    }

    VkAccelerationStructureGeometryKHR makeInstanceGeometry(VkDeviceAddress instanceBufferAddress)
    {
        VkAccelerationStructureGeometryInstancesDataKHR instancesData{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
        instancesData.arrayOfPointers = VK_FALSE;
        instancesData.data.deviceAddress = instanceBufferAddress;

        VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances = instancesData;
        return geometry;
    }
}

BuildSizes AccelerationStructureBuilder::queryBottomLevelSizes(const TriangleGeometryDesc &geometry,
                                                               VkBuildAccelerationStructureFlagsKHR flags)
{
    BuildSizes sizes{};

    auto context = core::VulkanContext::getContext();
    if (!context || geometry.vertexAddress == 0u || geometry.indexAddress == 0u || geometry.triangleCount == 0u)
        return sizes;

    auto vkGeometry = makeTriangleGeometry(geometry);

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = flags;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1u;
    buildInfo.pGeometries = &vkGeometry;

    const uint32_t primitiveCount = geometry.triangleCount;
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(context->getDevice(),
                                            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                            &buildInfo,
                                            &primitiveCount,
                                            &sizeInfo);

    sizes.accelerationStructureSize = sizeInfo.accelerationStructureSize;
    sizes.buildScratchSize = sizeInfo.buildScratchSize;
    sizes.updateScratchSize = sizeInfo.updateScratchSize;
    return sizes;
}

BuildSizes AccelerationStructureBuilder::queryTopLevelSizes(uint32_t instanceCount,
                                                            VkBuildAccelerationStructureFlagsKHR flags)
{
    BuildSizes sizes{};

    auto context = core::VulkanContext::getContext();
    if (!context || instanceCount == 0u)
        return sizes;

    auto geometry = makeInstanceGeometry(0u);

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = flags;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1u;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(context->getDevice(),
                                            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                            &buildInfo,
                                            &instanceCount,
                                            &sizeInfo);

    sizes.accelerationStructureSize = sizeInfo.accelerationStructureSize;
    sizes.buildScratchSize = sizeInfo.buildScratchSize;
    sizes.updateScratchSize = sizeInfo.updateScratchSize;
    return sizes;
}

void AccelerationStructureBuilder::recordBottomLevelBuild(core::CommandBuffer &commandBuffer,
                                                          AccelerationStructure &accelerationStructure,
                                                          const TriangleGeometryDesc &geometry,
                                                          core::Buffer &scratchBuffer,
                                                          VkBuildAccelerationStructureFlagsKHR flags,
                                                          VkBuildAccelerationStructureModeKHR mode,
                                                          VkAccelerationStructureKHR srcAccelerationStructure)
{
    if (!accelerationStructure.isValid() || geometry.vertexAddress == 0u || geometry.indexAddress == 0u || geometry.triangleCount == 0u)
        return;

    const VkDeviceAddress scratchAddress = getBufferDeviceAddress(scratchBuffer);
    if (scratchAddress == 0u)
        return;

    auto vkGeometry = makeTriangleGeometry(geometry);

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = flags;
    buildInfo.mode = mode;
    buildInfo.srcAccelerationStructure = srcAccelerationStructure;
    buildInfo.dstAccelerationStructure = accelerationStructure.vk();
    buildInfo.geometryCount = 1u;
    buildInfo.pGeometries = &vkGeometry;
    buildInfo.scratchData.deviceAddress = scratchAddress;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = geometry.triangleCount;
    const VkAccelerationStructureBuildRangeInfoKHR *ranges[] = {&rangeInfo};

    vkCmdBuildAccelerationStructuresKHR(commandBuffer.vk(), 1u, &buildInfo, ranges);
}

void AccelerationStructureBuilder::recordTopLevelBuild(core::CommandBuffer &commandBuffer,
                                                       AccelerationStructure &accelerationStructure,
                                                       VkDeviceAddress instanceBufferAddress,
                                                       uint32_t instanceCount,
                                                       core::Buffer &scratchBuffer,
                                                       VkBuildAccelerationStructureFlagsKHR flags,
                                                       VkBuildAccelerationStructureModeKHR mode,
                                                       VkAccelerationStructureKHR srcAccelerationStructure)
{
    if (!accelerationStructure.isValid() || instanceBufferAddress == 0u || instanceCount == 0u)
        return;

    const VkDeviceAddress scratchAddress = getBufferDeviceAddress(scratchBuffer);
    if (scratchAddress == 0u)
        return;

    auto geometry = makeInstanceGeometry(instanceBufferAddress);

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = flags;
    buildInfo.mode = mode;
    buildInfo.srcAccelerationStructure = srcAccelerationStructure;
    buildInfo.dstAccelerationStructure = accelerationStructure.vk();
    buildInfo.geometryCount = 1u;
    buildInfo.pGeometries = &geometry;
    buildInfo.scratchData.deviceAddress = scratchAddress;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = instanceCount;
    const VkAccelerationStructureBuildRangeInfoKHR *ranges[] = {&rangeInfo};

    vkCmdBuildAccelerationStructuresKHR(commandBuffer.vk(), 1u, &buildInfo, ranges);
}

VkAccelerationStructureInstanceKHR AccelerationStructureBuilder::toVkInstance(const InstanceDesc &instance)
{
    VkAccelerationStructureInstanceKHR vkInstance{};
    vkInstance.transform = instance.transform;
    vkInstance.instanceCustomIndex = instance.customIndex;
    vkInstance.mask = instance.mask;
    vkInstance.instanceShaderBindingTableRecordOffset = instance.instanceShaderBindingTableRecordOffset;
    vkInstance.flags = instance.flags;
    vkInstance.accelerationStructureReference = instance.blasDeviceAddress;
    return vkInstance;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
