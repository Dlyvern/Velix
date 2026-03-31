#ifndef ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP
#define ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP

#include "Core/Macros.hpp"

#include "Engine/Entity.hpp"
#include "Engine/EnvironmentSettings.hpp"
#include "Engine/Mesh.hpp"

#include "Engine/Builders/GraphicsPipelineKey.hpp"

#include <memory>
#include <string>
#include <vector>
#include <array>
#include <cstddef>
#include <limits>
#include <unordered_map>

#include <glm/glm.hpp>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct DrawItem
{
    struct DrawMeshState
    {
        GPUMesh::SharedPtr mesh{nullptr};
        MeshGeometryHash geometryHash{};
        glm::mat4 localTransform{1.0f};
        glm::vec3 localBoundsCenter{0.0f};
        float localBoundsRadius{0.0f};
        glm::vec3 worldBoundsCenter{0.0f};
        float worldBoundsRadius{0.0f};
    };

    std::vector<DrawMeshState> meshStates;
    glm::mat4 transform{1.0f};
    std::vector<glm::mat4> finalBones;
    uint32_t bonesOffset{0};

    GraphicsPipelineKey graphicsPipelineKey;
};

struct RenderGraphLightData
{
    glm::vec4 position{0.0f};
    glm::vec4 direction{0.0f, 0.0f, -1.0f, 0.0f};
    glm::vec4 colorStrength{1.0f};
    glm::vec4 parameters{1.0f};
    glm::vec4 shadowInfo{0.0f}; // x = casts shadow, y = shadow index, z = far/range, w = near
};

struct PerObjectInstanceData
{
    glm::mat4 model{1.0f};
    glm::uvec4 objectInfo{0u}; // x = objectId, y = bonesOffset, z = materialIndex, w = reserved
};

// World-space bounding sphere for a draw batch (aggregate of all instances).
// Uploaded to GPU for compute-shader frustum culling.
struct GPUBatchBounds
{
    glm::vec3 center{0.0f};
    float radius{0.0f}; // <= 0 means "always visible" (no bounds data)
};

struct DrawBatch
{
    GPUMesh::SharedPtr mesh{nullptr};
    Material::SharedPtr material{nullptr};
    bool skinned{false};
    uint32_t firstInstance{0};
    uint32_t instanceCount{0};
};

struct RTReflectionShadingInstanceData
{
    uint64_t vertexAddress{0u};
    uint64_t indexAddress{0u};
    uint32_t vertexStride{0u};
    uint32_t padding0{0u};
    uint32_t padding1{0u};
    uint32_t padding2{0u};
    Material::GPUParams material{};
};

class RenderGraphPassContext
{
public:
    uint32_t currentFrame{0};
    uint32_t currentImageIndex{0};
    uint32_t executionIndex{0};
    uint32_t activeDirectionalShadowCount{0};
    uint32_t activeSpotShadowCount{0};
    uint32_t activePointShadowCount{0};
    uint32_t activeRTShadowLayerCount{0};
};

struct ShadowConstants
{
    static constexpr uint32_t MAX_DIRECTIONAL_CASCADES = 4;
    static constexpr uint32_t MAX_SPOT_SHADOWS = 3;
    static constexpr uint32_t MAX_POINT_SHADOWS = 1;
    static constexpr uint32_t POINT_SHADOW_FACES = 6;
};

struct RenderGraphLightSpaceMatrixUBO
{
    glm::mat4 lightSpaceMatrix{1.0f};
    std::array<glm::mat4, ShadowConstants::MAX_DIRECTIONAL_CASCADES> directionalLightSpaceMatrices;
    glm::vec4 directionalCascadeSplits{glm::vec4(std::numeric_limits<float>::max())};
    std::array<glm::mat4, ShadowConstants::MAX_SPOT_SHADOWS> spotLightSpaceMatrices;

    RenderGraphLightSpaceMatrixUBO()
    {
        directionalLightSpaceMatrices.fill(1.0f);
        spotLightSpaceMatrices.fill(1.0f);
    }
};

class RenderGraphPassPerFrameData
{
public:
    std::unordered_map<Entity *, DrawItem> drawItems;
    std::vector<PerObjectInstanceData> perObjectInstances;
    std::vector<DrawBatch> drawBatches;
    std::vector<RTReflectionShadingInstanceData> rtReflectionShadingInstances;
    std::array<std::vector<DrawBatch>, ShadowConstants::MAX_DIRECTIONAL_CASCADES> directionalShadowDrawBatches;
    std::array<std::vector<DrawBatch>, ShadowConstants::MAX_SPOT_SHADOWS> spotShadowDrawBatches;
    std::array<std::vector<DrawBatch>, ShadowConstants::MAX_POINT_SHADOWS * ShadowConstants::POINT_SHADOW_FACES> pointShadowDrawBatches;

    glm::vec3 directionalLightDirection;
    float directionalLightStrength;
    bool hasDirectionalLight{false};
    bool skyLightEnabled{false};

    glm::mat4 lightSpaceMatrix;
    std::array<glm::mat4, ShadowConstants::MAX_DIRECTIONAL_CASCADES> directionalLightSpaceMatrices{};
    std::array<float, ShadowConstants::MAX_DIRECTIONAL_CASCADES> directionalCascadeSplits{};
    std::array<glm::mat4, ShadowConstants::MAX_SPOT_SHADOWS> spotLightSpaceMatrices{};
    std::array<glm::mat4, ShadowConstants::MAX_POINT_SHADOWS * ShadowConstants::POINT_SHADOW_FACES> pointLightSpaceMatrices{};
    uint32_t activeDirectionalCascadeCount{0};
    uint32_t activeSpotShadowCount{0};
    uint32_t activePointShadowCount{0};
    uint32_t activeRTShadowLayerCount{0};
    VkViewport swapChainViewport;
    VkRect2D swapChainScissor;

    VkDescriptorSet cameraDescriptorSet;
    VkDescriptorSet previewCameraDescriptorSet;
    VkDescriptorSet perObjectDescriptorSet;
    VkDescriptorSet shadowPerObjectDescriptorSet;

    float deltaTime;
    float elapsedTime{0.0f};

    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 previewView;
    glm::mat4 previewProjection;

    std::string skyboxHDRPath;
    FogSettings fogSettings{};
    size_t fogSettingsHash{0u};

    // Per-batch bounding spheres for GPU frustum culling (parallel to drawBatches).
    std::vector<GPUBatchBounds> batchBounds;

    // GPU indirect draw buffer (VkDrawIndexedIndirectCommand[]), one entry per drawBatch.
    // Written by CPU each frame, then optionally modified by the GPU culling compute pass.
    // VK_NULL_HANDLE when not yet initialised.
    VkBuffer indirectDrawBuffer{VK_NULL_HANDLE};

    // Unified geometry buffer handles (VK_NULL_HANDLE when not available).
    // GBuffer binds these once instead of rebinding per-batch when all static
    // batches use the unified layout.
    VkBuffer unifiedStaticVertexBuffer{VK_NULL_HANDLE};
    VkBuffer unifiedStaticIndexBuffer{VK_NULL_HANDLE};

    // Bindless material descriptor set — holds all textures (binding 0) and
    // the MaterialParams SSBO (binding 1). Bound once at Set 1 in GBuffer.
    VkDescriptorSet bindlessDescriptorSet{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP
