#ifndef ELIX_TRANSFORM_3D_PROPERTY_HPP
#define ELIX_TRANSFORM_3D_PROPERTY_HPP

#define GLM_ENABLE_EXPERIMENTAL

#include "Core/Macros.hpp"

#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(properties)

class Transform3DProperty
{
    void setPosition(const glm::vec3 &position);
    void setScale(const glm::vec3 &scale);
    void setRotation(const glm::quat &quat);
    void setRotation(const glm::vec3 &rotation);

    glm::vec3 getEulerDegrees() const;
    void setEulerDegrees(const glm::vec3 &eulerDeg);

    const glm::vec3 &getPosition() const;
    glm::vec3 &getPosition();
    const glm::vec3 &getScale() const;
    glm::vec3 &getScale();
    const glm::quat &getRotation() const;
    glm::mat4 getMatrix() const;

private:
    glm::vec3 m_position{0.0f};
    glm::vec3 m_scale{1.0f};
    glm::quat m_rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_TRANSFORM_3D_PROPERTY_HPP