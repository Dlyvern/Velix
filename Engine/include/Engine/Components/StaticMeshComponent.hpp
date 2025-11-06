#ifndef ELIX_STATIC_MESH_COMPONENT_HPP
#define ELIX_STATIC_MESH_COMPONENT_HPP

#include "Core/Macros.hpp"
#include "Engine/Components/ECS.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/Material.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class StaticMeshComponent : public ECS
{
public:
    explicit StaticMeshComponent(const std::vector<Mesh3D>& meshes);

    const std::vector<Mesh3D>& getMeshes() const;
    Mesh3D& getMesh(int index);
private:
    std::vector<Mesh3D> m_meshes;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_STATIC_MESH_COMPONENT_HPP