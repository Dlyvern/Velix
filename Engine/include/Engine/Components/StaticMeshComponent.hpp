#ifndef ELIX_STATIC_MESH_COMPONENT_HPP
#define ELIX_STATIC_MESH_COMPONENT_HPP

#include "Core/Macros.hpp"
#include "Engine/Components/ECS.hpp"
#include "Engine/Mesh.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class StaticMeshComponent : public ECS
{
public:
    explicit StaticMeshComponent(const std::vector<CPUMesh> &meshes);

    const std::vector<CPUMesh> &getMeshes() const;
    CPUMesh &getMesh(int index);

private:
    std::vector<CPUMesh> m_meshes;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_STATIC_MESH_COMPONENT_HPP