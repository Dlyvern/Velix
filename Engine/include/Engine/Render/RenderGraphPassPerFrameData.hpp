#ifndef ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP
#define ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP 

#include "Core/Macros.hpp"

#include "Engine/Entity.hpp"
#include "Engine/Mesh.hpp"

#include <map>
#include <memory>

#include <glm/glm.hpp>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct GPUEntity
{
    std::shared_ptr<GPUMesh> mesh;
    glm::mat4 transform;
    VkDescriptorSet materialDescriptorSet;
};

class RenderGraphPassPerFrameData
{
public:
    //!For now, leave it like this, after adding SkeletonMeshProxy it needs to be redesigned
    std::map<Entity::SharedPtr, std::shared_ptr<GPUMesh>> transformationBasedOnMesh;
    std::map<Entity::SharedPtr, GPUEntity> meshes;

    glm::mat4 lightSpaceMatrix;
    VkViewport swapChainViewport;
    VkRect2D swapChainScissor;
    VkDescriptorSet cameraDescriptorSet;
    VkDescriptorSet lightDescriptorSet;

    //TODO THIS IS SO FUCKED UP, MAKE IT BETTER ASAP
    std::vector<VkImageView> viewportImageViews;
    bool isViewportImageViewsDirty{true};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_RENDER_GRAPH_PASS_PER_FRAME_DATA_HPP