#ifndef ELIX_MESH_TO_RAY_TRACED_OBJECTS_CONVERTER_HPP
#define ELIX_MESH_TO_RAY_TRACED_OBJECTS_CONVERTER_HPP

#include "Core/Macros.hpp"
#include "Engine/Mesh.hpp"

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(rayTracing)

class MeshToRayTracedObjectsConverter
{
public:
    static bool convert(GPUMesh &mesh,
                        VkAccelerationStructureGeometryKHR &geometry,
                        VkAccelerationStructureBuildRangeInfoKHR &rangeInfo);
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_MESH_TO_RAY_TRACED_OBJECTS_CONVERTER_HPP
