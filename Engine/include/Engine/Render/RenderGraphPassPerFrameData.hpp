#ifndef ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP
#define ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP

#include "Core/Macros.hpp"

#include "Engine/Entity.hpp"
#include "Engine/Mesh.hpp"

#include "Engine/Builders/GraphicsPipelineKey.hpp"

#include <memory>
#include <string>
#include <vector>
#include <array>
#include <cstddef>
#include <unordered_map>

#include <glm/glm.hpp>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct DrawItem
{
    std::vector<GPUMesh::SharedPtr> meshes;
    std::vector<size_t> localMeshGeometryHashes;
    std::vector<glm::vec3> localMeshBoundsCenters;
    std::vector<float> localMeshBoundsRadii;
    std::vector<glm::mat4> localMeshTransforms;
    std::vector<glm::vec3> cachedWorldBoundsCenters;
    std::vector<float> cachedWorldBoundsRadii;
    glm::mat4 transform;
    std::vector<glm::mat4> finalBones;
    uint32_t bonesOffset{0};

    GraphicsPipelineKey graphicsPipelineKey;
};

struct PerObjectInstanceData
{
    glm::mat4 model{1.0f};
    glm::uvec4 objectInfo{0u}; // x = objectId, y = bonesOffset, z/w = reserved
};

struct DrawBatch
{
    GPUMesh::SharedPtr mesh{nullptr};
    Material::SharedPtr material{nullptr};
    bool skinned{false};
    uint32_t firstInstance{0};
    uint32_t instanceCount{0};
};

class AdditionalPerFrameData
{
public:
    std::vector<DrawItem> drawItems;
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
};

struct ShadowConstants
{
    static constexpr uint32_t MAX_DIRECTIONAL_CASCADES = 4;
    static constexpr uint32_t MAX_SPOT_SHADOWS = 3;
    static constexpr uint32_t MAX_POINT_SHADOWS = 1;
    static constexpr uint32_t POINT_SHADOW_FACES = 6;
};

class RenderGraphPassPerFrameData
{
public:
    std::unordered_map<Entity *, DrawItem> drawItems;
    std::vector<PerObjectInstanceData> perObjectInstances;
    std::vector<DrawBatch> drawBatches;
    std::array<std::vector<DrawBatch>, ShadowConstants::MAX_DIRECTIONAL_CASCADES> directionalShadowDrawBatches;
    std::array<std::vector<DrawBatch>, ShadowConstants::MAX_SPOT_SHADOWS> spotShadowDrawBatches;
    std::array<std::vector<DrawBatch>, ShadowConstants::MAX_POINT_SHADOWS * ShadowConstants::POINT_SHADOW_FACES> pointShadowDrawBatches;

    std::vector<AdditionalPerFrameData> additionalData;

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
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP
