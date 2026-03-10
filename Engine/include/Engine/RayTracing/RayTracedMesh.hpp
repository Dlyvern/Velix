#ifndef ELIX_RAY_TRACED_MESH_HPP
#define ELIX_RAY_TRACED_MESH_HPP

#include "Core/Macros.hpp"
#include "Engine/Mesh.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(rayTracing)

struct RayTracedMesh
{
    using SharedPtr = std::shared_ptr<RayTracedMesh>;

    core::Buffer::SharedPtr indexBuffer{nullptr};
    core::Buffer::SharedPtr vertexBuffer{nullptr};
    VkDeviceSize positionOffset{0u};
    VkFormat positionFormat{VK_FORMAT_R32G32B32_SFLOAT};

    static SharedPtr createFromMesh(const GPUMesh &mesh, core::CommandPool::SharedPtr commandPool = nullptr);

    bool uploadFromMesh(const GPUMesh &mesh, core::CommandPool::SharedPtr commandPool = nullptr);
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RAY_TRACED_MESH_HPP
