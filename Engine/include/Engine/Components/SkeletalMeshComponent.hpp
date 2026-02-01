#ifndef ELIX_SKELETAL_MESH_COMPONENT_HPP
#define ELIX_SKELETAL_MESH_COMPONENT_HPP

#include "Core/Macros.hpp"

#include "Engine/Components/ECS.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/Skeleton.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class SkeletalMeshComponent : public ECS
{
public:
    SkeletalMeshComponent(const std::vector<CPUMesh> &meshes, const Skeleton &skeleton);
    const std::vector<CPUMesh> &getMeshes() const;
    CPUMesh &getMesh(int index);

    const Skeleton &getSkeleton() const;
    Skeleton &getSkeleton();

private:
    std::vector<CPUMesh> m_meshes;
    Skeleton m_skeleton;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SKELETAL_MESH_COMPONENT_HPP