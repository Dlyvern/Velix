#include "Engine/Components/StaticMeshComponent.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

StaticMeshComponent::StaticMeshComponent(const std::vector<CPUMesh> &meshes) : m_meshes(meshes)
{
    m_perMeshMaterialOverrides.resize(m_meshes.size(), nullptr);
    m_perMeshMaterialOverridePaths.resize(m_meshes.size());
}

StaticMeshComponent::StaticMeshComponent(const std::string &assetPath)
    : m_assetPath(assetPath),
      m_modelHandle(assetPath)
{
}

const std::vector<CPUMesh> &StaticMeshComponent::getMeshes() const
{
    return m_meshes;
}

CPUMesh &StaticMeshComponent::getMesh(int index)
{
    return m_meshes[index];
}

bool StaticMeshComponent::isReady() const
{
    // Ready if meshes were set via legacy constructor OR handle resolved.
    if (!m_meshes.empty())
        return true;
    return m_modelHandle.ready();
}

void StaticMeshComponent::onModelLoaded()
{
    if (!m_modelHandle.ready())
        return;
    const auto &asset = m_modelHandle.get();
    m_meshes = asset.meshes;
    m_perMeshMaterialOverrides.resize(m_meshes.size(), nullptr);
    m_perMeshMaterialOverridePaths.resize(m_meshes.size());
}

ELIX_NESTED_NAMESPACE_END
