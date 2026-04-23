#ifndef ELIX_SPRITE_COMPONENT_HPP
#define ELIX_SPRITE_COMPONENT_HPP

#include "Core/Macros.hpp"
#include "Engine/Components/ECS.hpp"

#include <glm/glm.hpp>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class SpriteComponent : public ECS
{
public:
    std::string texturePath;

    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 uvRect{0.0f, 0.0f, 1.0f, 1.0f};
    glm::vec2 size{1.0f, 1.0f};
    float     rotation{0.0f};

    int  sortLayer{0};
    bool flipX{false};
    bool flipY{false};
    bool visible{true};
};

ELIX_NESTED_NAMESPACE_END

#endif
