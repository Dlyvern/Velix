#ifndef ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP
#define ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP

#include "Core/Macros.hpp"

#include "Engine/Entity.hpp"
#include "Engine/Mesh.hpp"

#include "Engine/Builders/GraphicsPipelineKey.hpp"

#include <map>
#include <memory>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct DrawItem
{
    std::vector<GPUMesh::SharedPtr> meshes;
    glm::mat4 transform;
    std::vector<glm::mat4> finalBones;

    MaterialRenderState materialState;

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

class RenderGraphPassPerFrameData
{
public:
    std::map<Entity::SharedPtr, DrawItem> drawItems;

    std::vector<AdditionalPerFrameData> additionalData;

    glm::mat4 lightSpaceMatrix;
    VkViewport swapChainViewport;
    VkRect2D swapChainScissor;

    VkDescriptorSet cameraDescriptorSet;
    VkDescriptorSet previewCameraDescriptorSet;
    VkDescriptorSet perObjectDescriptorSet;

    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 previewView;
    glm::mat4 previewProjection;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP