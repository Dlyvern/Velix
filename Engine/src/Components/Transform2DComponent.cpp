#include "Engine/Components/Transform2DComponent.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void Transform2DComponent::setPosition(const glm::vec2& position)
{
    m_position = position;
}

void Transform2DComponent::setScale(const glm::vec2& scale)
{
    m_scale = scale;
}

void Transform2DComponent::setRotation(float rotation)
{
    m_rotation = rotation;
}

glm::mat4 Transform2DComponent::getMatrix() const
{
    glm::mat4 model(1.0f);

    model = glm::translate(model, glm::vec3(m_position, 0.0f));
    model = glm::rotate(model, m_rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, glm::vec3(m_scale, 1.0f));

    return model;
}

const glm::vec2& Transform2DComponent::getPosition() const
{
    return m_position;
}

const glm::vec2& Transform2DComponent::getScale() const
{
    return m_scale;
}

float Transform2DComponent::getRotation() const
{
    return m_rotation;
}

ELIX_NESTED_NAMESPACE_END
