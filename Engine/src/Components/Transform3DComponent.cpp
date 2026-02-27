#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Entity.hpp"

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

glm::vec3 Transform3DComponent::getWorldPosition() const
{
    const glm::mat4 world = getMatrix();
    return glm::vec3(world[3]);
}

glm::quat Transform3DComponent::getWorldRotation() const
{
    const auto *parentTransform = getParentTransform();
    if (!parentTransform)
        return m_rotation;

    return parentTransform->getWorldRotation() * m_rotation;
}

void Transform3DComponent::setWorldPosition(const glm::vec3 &position)
{
    const auto *parentTransform = getParentTransform();
    if (!parentTransform)
    {
        m_position = position;
        return;
    }

    const glm::mat4 inverseParentMatrix = glm::inverse(parentTransform->getMatrix());
    m_position = glm::vec3(inverseParentMatrix * glm::vec4(position, 1.0f));
}

void Transform3DComponent::setWorldRotation(const glm::quat &rotation)
{
    const auto *parentTransform = getParentTransform();
    if (!parentTransform)
    {
        m_rotation = rotation;
        return;
    }

    m_rotation = glm::inverse(parentTransform->getWorldRotation()) * rotation;
}

glm::mat4 Transform3DComponent::getLocalMatrix() const
{
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_position);
    glm::mat4 rotationMat = glm::toMat4(m_rotation);
    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), m_scale);
    return translation * rotationMat * scaleMat;
}

glm::mat4 Transform3DComponent::getMatrix() const
{
    const auto *parentTransform = getParentTransform();
    if (!parentTransform)
        return getLocalMatrix();

    return parentTransform->getMatrix() * getLocalMatrix();
}

const Transform3DComponent *Transform3DComponent::getParentTransform() const
{
    auto *owner = getOwner<Entity>();
    if (!owner)
        return nullptr;

    auto *parent = owner->getParent();
    if (!parent)
        return nullptr;

    return parent->getComponent<Transform3DComponent>();
}

ELIX_NESTED_NAMESPACE_END
