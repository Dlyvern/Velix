#ifndef ELIX_RAY_TRACING_SCENE_HPP
#define ELIX_RAY_TRACING_SCENE_HPP

#include "Core/Macros.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/RayTracing/AccelerationStructure.hpp"
#include "Engine/RayTracing/RayTracingGeometryCache.hpp"

#include <glm/mat4x4.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(rayTracing)

class RayTracingScene
{
public:
    struct InstanceInput
    {
        std::size_t geometryHash{0u};
        GPUMesh::SharedPtr mesh{nullptr};
        glm::mat4 transform{1.0f};
        uint32_t customInstanceIndex{0u};
        uint8_t mask{0xFFu};
        bool forceOpaque{true};
        bool disableTriangleFacingCull{false};
    };

    explicit RayTracingScene(uint32_t framesInFlight = 2u);

    bool update(uint32_t frameIndex,
                const std::vector<InstanceInput> &instances,
                RayTracingGeometryCache &geometryCache,
                core::CommandPool::SharedPtr commandPool = nullptr);

    void clear();

    const AccelerationStructure::SharedPtr &getTLAS(uint32_t frameIndex) const;
    uint32_t getInstanceCount(uint32_t frameIndex) const;

private:
    struct FrameState
    {
        core::Buffer::SharedPtr instanceBuffer{nullptr};
        AccelerationStructure::SharedPtr tlas{nullptr};
        uint32_t instanceCount{0u};
        std::size_t contentHash{0u};
    };

    std::vector<FrameState> m_frames;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RAY_TRACING_SCENE_HPP
