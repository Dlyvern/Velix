#ifndef ELIX_CAMERA_COMPONENT_HPP
#define ELIX_CAMERA_COMPONENT_HPP

#include "Core/Macros.hpp"
#include "Engine/Components/ECS.hpp"
#include "Engine/Camera.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class CameraComponent : public ECS
{
public:
    explicit CameraComponent(Camera::SharedPtr camera);
    CameraComponent();

    Camera::SharedPtr getCamera() const;

private:
    Camera::SharedPtr m_camera{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_CAMERA_COMPONENT_HPP