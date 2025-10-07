#ifndef ELIX_SPRITE_MESH_COMPONENT_HPP
#define ELIX_SPRITE_MESH_COMPONENT_HPP

#include "Core/Macros.hpp"
#include "Engine/Components/ECS.hpp"
#include "Engine/Mesh.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class SpriteMeshComponent : public ECS
{
public:
    explicit SpriteMeshComponent(const Mesh2D& mesh);
    const Mesh2D& getMesh() const;

private:
    Mesh2D m_mesh;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_SPRITE_MESH_COMPONENT_HPP