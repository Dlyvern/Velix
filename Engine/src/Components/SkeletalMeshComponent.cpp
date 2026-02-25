#include "Engine/Components/SkeletalMeshComponent.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

SkeletalMeshComponent::SkeletalMeshComponent(const std::vector<CPUMesh> &meshes, const Skeleton &skeleton) : m_meshes(meshes),
                                                                                                             m_skeleton(skeleton)
{
    m_skeleton.calculateBindPoseTransforms();
    m_perMeshMaterialOverrides.resize(m_meshes.size(), nullptr);
    m_perMeshMaterialOverridePaths.resize(m_meshes.size());
}

const std::vector<CPUMesh> &SkeletalMeshComponent::getMeshes() const
{
    return m_meshes;
}

CPUMesh &SkeletalMeshComponent::getMesh(int index)
{
    return m_meshes[index];
}

const Skeleton &SkeletalMeshComponent::getSkeleton() const
{
    return m_skeleton;
}

Skeleton &SkeletalMeshComponent::getSkeleton()
{
    return m_skeleton;
}

ELIX_NESTED_NAMESPACE_END
