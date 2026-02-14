#include "Engine/Properties/Transform3DProperty.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(properties)

void Transform3DProperty::setPosition(const glm::vec3 &position)
{
    m_position = position;
}

void Transform3DProperty::setScale(const glm::vec3 &scale)
{
    m_scale = scale;
}

void Transform3DProperty::setRotation(const glm::quat &quat)
{
    m_rotation = quat;
}

void Transform3DProperty::setRotation(const glm::vec3 &rotation)
{
    setEulerDegrees(rotation);
}

glm::vec3 Transform3DProperty::getEulerDegrees() const
{
    return glm::degrees(glm::eulerAngles(m_rotation));
}

void Transform3DProperty::setEulerDegrees(const glm::vec3 &eulerDeg)
{
    m_rotation = glm::quat(glm::radians(eulerDeg));
}

const glm::vec3 &Transform3DProperty::getPosition() const
{
    return m_position;
}

glm::vec3 &Transform3DProperty::getPosition()
{
    return m_position;
}

const glm::vec3 &Transform3DProperty::getScale() const
{
    return m_scale;
}

glm::vec3 &Transform3DProperty::getScale()
{
    return m_scale;
}

const glm::quat &Transform3DProperty::getRotation() const
{
    return m_rotation;
}

glm::mat4 Transform3DProperty::getMatrix() const
{
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_position);
    glm::mat4 rotationMat = glm::toMat4(m_rotation);
    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), m_scale);
    return translation * rotationMat * scaleMat;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
