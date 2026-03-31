#ifndef ELIX_RAY_TRACED_MESH_HPP
#define ELIX_RAY_TRACED_MESH_HPP

#include "Core/Macros.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/RTX/AccelerationStructureBuilder.hpp"

#include "Engine/Mesh.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(rayTracing)

class RayTracedMesh
{
public:
    using SharedPtr = std::shared_ptr<RayTracedMesh>;

    struct UploadSubmission
    {
        VkFence fence{VK_NULL_HANDLE};
        core::CommandBuffer::SharedPtr commandBuffer{nullptr};
    };

    core::Buffer::SharedPtr indexBuffer{nullptr};
    core::Buffer::SharedPtr vertexBuffer{nullptr};
    VkDeviceSize positionOffset{0u};
    VkFormat positionFormat{VK_FORMAT_R32G32B32_SFLOAT};

    static SharedPtr createFromMesh(const GPUMesh &mesh,
                                    core::CommandPool::SharedPtr commandPool = nullptr,
                                    UploadSubmission *outPendingUpload = nullptr);

    static bool fillGeometryDesc(const GPUMesh &mesh,
                                 const RayTracedMesh &rayTracedMesh,
                                 core::rtx::TriangleGeometryDesc &geometryDesc);

private:
    bool uploadFromMesh(const GPUMesh &mesh,
                        core::CommandPool::SharedPtr commandPool = nullptr,
                        UploadSubmission *outPendingUpload = nullptr);
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RAY_TRACED_MESH_HPP
