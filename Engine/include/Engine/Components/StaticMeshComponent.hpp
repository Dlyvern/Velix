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
    explicit StaticMeshComponent(const Mesh3D& mesh);

    const Mesh3D& getMesh() const;

    void setMaterial(Material::SharedPtr material);

    Material::SharedPtr getMaterial() const;
private:
    Mesh3D m_mesh;
    Material::SharedPtr m_material{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_STATIC_MESH_COMPONENT_HPP