#include "Engine/Components/CameraComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Entity.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

CameraComponent::CameraComponent(Camera::SharedPtr camera) : m_camera(camera)
{
}

CameraComponent::CameraComponent()
{
    m_camera = std::make_shared<Camera>();
}

Camera::SharedPtr CameraComponent::getCamera() const
{
    syncFromOwnerTransform();
    return m_camera;
}

void CameraComponent::update(float deltaTime)
{
    (void)deltaTime;
    syncFromOwnerTransform();
}

void CameraComponent::syncFromOwnerTransform() const
{
    if (!m_camera)
        return;

    auto *owner = getOwner<Entity>();
    if (!owner)
        return;

    auto *transform = owner->getComponent<Transform3DComponent>();
    if (!transform)
        return;

    const glm::vec3 worldOffset = transform->getWorldRotation() * m_positionOffset;
    m_camera->setPosition(transform->getWorldPosition() + worldOffset);
}

void CameraComponent::onOwnerAttached()
{
    syncFromOwnerTransform();
}

void CameraComponent::setPositionOffset(const glm::vec3 &offset)
{
    m_positionOffset = offset;
    syncFromOwnerTransform();
}

const glm::vec3 &CameraComponent::getPositionOffset() const
{
    return m_positionOffset;
}

ELIX_NESTED_NAMESPACE_END
