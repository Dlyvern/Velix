#include "Engine/Components/StaticMeshComponent.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

StaticMeshComponent::StaticMeshComponent(const std::vector<Mesh3D>& meshes) : m_meshes(meshes)
{
    
}

const std::vector<Mesh3D>& StaticMeshComponent::getMeshes() const
{
    return m_meshes;
}

Mesh3D& StaticMeshComponent::getMesh(int index)
{
    return m_meshes[index];
}

ELIX_NESTED_NAMESPACE_END