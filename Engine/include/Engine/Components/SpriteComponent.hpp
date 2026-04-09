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

    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};   // RGBA tint applied on top of the texture
    glm::vec4 uvRect{0.0f, 0.0f, 1.0f, 1.0f};   // u0, v0, u1, v1 sub-region of the texture
    glm::vec2 size{1.0f, 1.0f};                   // World-space width and height of the sprite quad
    float     rotation{0.0f};                      // Billboard rotation around the camera forward axis (radians)

    int  sortLayer{0};   // Higher values are drawn on top of lower values
    bool flipX{false};
    bool flipY{false};
    bool visible{true};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SPRITE_COMPONENT_HPP
