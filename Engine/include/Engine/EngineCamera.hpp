#ifndef ELIX_ENGINE_CAMERA_HPP
#define ELIX_ENGINE_CAMERA_HPP

#include "Core/Macros.hpp"

#include "Engine/Camera.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class EngineCamera
{
public:
    EngineCamera(Camera::SharedPtr camera);
    void update(float deltaTime);

private:
    float m_movementSpeed{3.0f};

    float m_mouseSensitivity{0.1f};
    bool m_firstClick{true};

    Camera::SharedPtr m_camera{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_ENGINE_CAMERA_HPP