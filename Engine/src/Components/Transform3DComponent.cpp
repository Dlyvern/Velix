#include "Engine/Components/Transform3DComponent.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void Transform3DComponent::setPosition(const glm::vec3& position)
{
    m_position = position;
}

void Transform3DComponent::setScale(const glm::vec3& scale)
{
    m_scale = scale;
}

void Transform3DComponent::setRotation(const glm::quat& quat)
{
    m_rotation = quat;
}

const glm::vec3& Transform3DComponent::getPosition() const
{
    return m_position;
}

const glm::vec3& Transform3DComponent::getScale() const
{
    return m_scale;
}

glm::vec3 Transform3DComponent::getEulerDegrees() const
{
    return glm::degrees(glm::eulerAngles(m_rotation));
}

void Transform3DComponent::setEulerDegrees(const glm::vec3& eulerDeg)
{
    m_rotation = glm::quat(glm::radians(eulerDeg));
}

glm::vec3& Transform3DComponent::getScale()
{
    return m_scale;
}

glm::vec3& Transform3DComponent::getPosition()
{
    return m_position;
}

const glm::quat& Transform3DComponent::getRotation() const
{
    return m_rotation;
}

glm::mat4 Transform3DComponent::getMatrix() const
{
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_position);
    glm::mat4 rotationMat = glm::toMat4(m_rotation);
    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), m_scale);
    return translation * rotationMat * scaleMat;
}

ELIX_NESTED_NAMESPACE_END
