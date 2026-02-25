#include "Engine/Components/StaticMeshComponent.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

StaticMeshComponent::StaticMeshComponent(const std::vector<CPUMesh> &meshes) : m_meshes(meshes)
{
    m_perMeshMaterialOverrides.resize(m_meshes.size(), nullptr);
    m_perMeshMaterialOverridePaths.resize(m_meshes.size());
}

const std::vector<CPUMesh> &StaticMeshComponent::getMeshes() const
{
    return m_meshes;
}

CPUMesh &StaticMeshComponent::getMesh(int index)
{
    return m_meshes[index];
}

ELIX_NESTED_NAMESPACE_END
