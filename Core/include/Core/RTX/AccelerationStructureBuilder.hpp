#ifndef ELIX_CORE_RTX_ACCELERATION_STRUCTURE_BUILDER_HPP
#define ELIX_CORE_RTX_ACCELERATION_STRUCTURE_BUILDER_HPP

#include "Core/Buffer.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/Macros.hpp"
#include "Core/RTX/AccelerationStructure.hpp"

#include <volk.h>

#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(rtx)

struct TriangleGeometryDesc
{
    VkDeviceAddress vertexAddress{0u};
    VkDeviceAddress indexAddress{0u};
    VkDeviceSize vertexStride{0u};
    uint32_t vertexCount{0u};
    uint32_t triangleCount{0u};
    VkFormat vertexFormat{VK_FORMAT_R32G32B32_SFLOAT};
    VkIndexType indexType{VK_INDEX_TYPE_UINT32};
    VkGeometryFlagsKHR geometryFlags{0u};
};

struct InstanceDesc
{
    VkTransformMatrixKHR transform{};
    uint32_t customIndex{0u};
    uint8_t mask{0xFFu};
    uint32_t instanceShaderBindingTableRecordOffset{0u};
    VkGeometryInstanceFlagsKHR flags{0u};
    VkDeviceAddress blasDeviceAddress{0u};
};

struct BuildSizes
{
    VkDeviceSize accelerationStructureSize{0u};
    VkDeviceSize buildScratchSize{0u};
    VkDeviceSize updateScratchSize{0u};
};

class AccelerationStructureBuilder
{
public:
    static BuildSizes queryBottomLevelSizes(const TriangleGeometryDesc &geometry,
                                            VkBuildAccelerationStructureFlagsKHR flags);

    static BuildSizes queryTopLevelSizes(uint32_t instanceCount,
                                         VkBuildAccelerationStructureFlagsKHR flags);

    static void recordBottomLevelBuild(core::CommandBuffer &commandBuffer,
                                       AccelerationStructure &accelerationStructure,
                                       const TriangleGeometryDesc &geometry,
                                       core::Buffer &scratchBuffer,
                                       VkBuildAccelerationStructureFlagsKHR flags,
                                       VkBuildAccelerationStructureModeKHR mode,
                                       VkAccelerationStructureKHR srcAccelerationStructure = VK_NULL_HANDLE);

    static void recordTopLevelBuild(core::CommandBuffer &commandBuffer,
                                    AccelerationStructure &accelerationStructure,
                                    VkDeviceAddress instanceBufferAddress,
                                    uint32_t instanceCount,
                                    core::Buffer &scratchBuffer,
                                    VkBuildAccelerationStructureFlagsKHR flags,
                                    VkBuildAccelerationStructureModeKHR mode,
                                    VkAccelerationStructureKHR srcAccelerationStructure = VK_NULL_HANDLE);

    static VkAccelerationStructureInstanceKHR toVkInstance(const InstanceDesc &instance);
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_CORE_RTX_ACCELERATION_STRUCTURE_BUILDER_HPP
