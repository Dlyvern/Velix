#include "Engine/Components/SpriteMeshComponent.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

SpriteMeshComponent::SpriteMeshComponent(const CPUMesh &mesh) : m_mesh(mesh)
{
}

const CPUMesh &SpriteMeshComponent::getMesh() const
{
    return m_mesh;
}

ELIX_NESTED_NAMESPACE_END