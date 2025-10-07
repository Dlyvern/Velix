#ifndef ELIX_STATIC_MESH_RENDER_GRAPH_PROXY_HPP
#define ELIX_STATIC_MESH_RENDER_GRAPH_PROXY_HPP

#include "Core/Macros.hpp"

#include "Engine/Render/Proxies/IRenderGraphProxy.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/Entity.hpp"

#include <memory>

#include <glm/mat4x4.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class StaticMeshRenderGraphProxy : public IRenderGraphProxy<GPUMesh, RenderGraphProxyContainerMapPtrData, size_t, GPUMesh>
{
public:
    using SharedPtr = std::shared_ptr<StaticMeshRenderGraphProxy>;

    //For now, leave it like this, after adding SkeletonMeshProxy it needs to be redesigned
    std::map<Entity::SharedPtr, std::shared_ptr<GPUMesh>> transformationBasedOnMesh;

    explicit StaticMeshRenderGraphProxy(const std::string& name) : IRenderGraphProxy(name) {}
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_STATIC_MESH_RENDER_GRAPH_PROXY_HPP