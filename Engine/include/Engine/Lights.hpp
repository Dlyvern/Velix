#ifndef ELIX_LIGHTS_HPP
#define ELIX_LIGHTS_HPP

#include "Core/Macros.hpp"

#include <glm/glm.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct BaseLight
{
    glm::vec3 color{1.0f};
    glm::vec3 position{1.0f};
    float strength{1.0f};
    bool castsShadows{true};

    virtual ~BaseLight() = default;
};

struct DirectionalLight : BaseLight
{
    glm::vec3 direction{-0.5f, -1.0f, -0.3f};
    bool skyLightEnabled{true};
};

struct PointLight : BaseLight
{
    float radius{10.0f};
    float falloff{10.0f};
};

struct SpotLight : BaseLight
{
    glm::vec3 direction{-0.5f, -1.0f, -0.3f};
    float innerAngle{15.0f};
    float outerAngle{30.0f};
    float range{10.0f};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_LIGHTS_HPP
