#include "Engine/Components/StaticMeshComponent.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

StaticMeshComponent::StaticMeshComponent(const Mesh3D& mesh) : m_mesh(mesh)
{
    
}

const Mesh3D& StaticMeshComponent::getMesh() const
{
    return m_mesh;
}

void StaticMeshComponent::setMaterial(Material::SharedPtr material)
{
    m_material = material;
}

Material::SharedPtr StaticMeshComponent::getMaterial() const
{
    return m_material;
}

ELIX_NESTED_NAMESPACE_END