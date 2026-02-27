#ifndef ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP
#define ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP

#include "Core/Macros.hpp"

#include "Engine/Entity.hpp"
#include "Engine/Mesh.hpp"

#include "Engine/Builders/GraphicsPipelineKey.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>

#include <glm/glm.hpp>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

// TODO maybe later -> one DrawItem = one mesh draw
// struct DrawItem
// {
//     GPUMesh::SharedPtr mesh;
//     glm::mat4 transform;
//     std::vector<glm::mat4> finalBones;

//     Material::SharedPtr material;
//     GraphicsPipelineKey graphicsPipelineKey;
// };

struct DrawItem
{
    std::vector<GPUMesh::SharedPtr> meshes;
    glm::mat4 transform;
    std::vector<glm::mat4> finalBones;
    uint32_t bonesOffset{0};

    GraphicsPipelineKey graphicsPipelineKey;
};

class AdditionalPerFrameData
{
public:
    std::vector<DrawItem> drawItems;
};

class RenderGraphPassContext
{
public:
    uint32_t currentFrame;
    uint32_t currentImageIndex;
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
    std::map<Entity::SharedPtr, DrawItem> drawItems;

    std::vector<AdditionalPerFrameData> additionalData;

    glm::vec3 directionalLightDirection;
    float directionalLightStrength;

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

    float deltaTime;

    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 previewView;
    glm::mat4 previewProjection;

    std::string skyboxHDRPath;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP
