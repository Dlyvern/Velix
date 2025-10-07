#include "Engine/Components/SpriteMeshComponent.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

SpriteMeshComponent::SpriteMeshComponent(const Mesh2D& mesh) : m_mesh(mesh)
{
    
}

const Mesh2D& SpriteMeshComponent::getMesh() const
{
    return m_mesh;
}

ELIX_NESTED_NAMESPACE_END