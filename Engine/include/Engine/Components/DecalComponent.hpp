#ifndef ELIX_DECAL_COMPONENT_HPP
#define ELIX_DECAL_COMPONENT_HPP

#include "Core/Macros.hpp"
#include "Engine/Components/ECS.hpp"
#include "Engine/Material.hpp"

#include <glm/vec3.hpp>
#include <memory>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class DecalComponent : public ECS
{
public:
    Material::SharedPtr material{nullptr}; // Domain must be DeferredDecal
    std::string materialPath;
    glm::vec3 size{1.0f, 1.0f, 1.0f}; // projection box half-extents
    float opacity{1.0f};              // [0–1] multiplied into alpha
    int sortOrder{0};                 // higher = rendered on top
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_DECAL_COMPONENT_HPP
