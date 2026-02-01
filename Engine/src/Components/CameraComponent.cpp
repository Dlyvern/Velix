#include "Engine/Components/CameraComponent.hpp"

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
    return m_camera;
}

ELIX_NESTED_NAMESPACE_END