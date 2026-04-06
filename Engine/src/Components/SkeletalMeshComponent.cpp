#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Entity.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

SkeletalMeshComponent::SkeletalMeshComponent(const std::vector<CPUMesh> &meshes, const Skeleton &skeleton)
    : m_meshes(meshes), m_skeleton(skeleton)
{
    m_skeleton.calculateBindPoseTransforms();
    m_perMeshMaterialOverrides.resize(m_meshes.size(), nullptr);
    m_perMeshMaterialOverridePaths.resize(m_meshes.size());
}

SkeletalMeshComponent::SkeletalMeshComponent(const std::string &assetPath)
    : m_assetPath(assetPath),
      m_modelHandle(assetPath)
{
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

bool SkeletalMeshComponent::isReady() const
{
    if (!m_meshes.empty())
        return true;
    return m_modelHandle.ready();
}

void SkeletalMeshComponent::onModelLoaded()
{
    if (!m_modelHandle.ready())
        return;
    const auto &asset = m_modelHandle.get();
    m_meshes = asset.meshes;
    if (asset.skeleton.has_value())
    {
        m_skeleton = asset.skeleton.value();
        m_skeleton.calculateBindPoseTransforms();
    }
    m_perMeshMaterialOverrides.resize(m_meshes.size(), nullptr);
    m_perMeshMaterialOverridePaths.resize(m_meshes.size());

    // Skeleton loaded asynchronously — notify the AnimatorComponent on the same
    // entity so it can bind the skeleton to its tree clips.
    if (asset.skeleton.has_value())
    {
        if (auto *owner = getOwner<Entity>())
        {
            if (auto *animator = owner->getComponent<AnimatorComponent>())
                animator->bindSkeleton(&m_skeleton);
        }
    }
}

ELIX_NESTED_NAMESPACE_END
