#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "ElixirCore/CameraComponent.hpp"

class Camera
{
public:
    explicit Camera(elix::CameraComponent* camera);

    void update(float deltaTime);
    
    elix::CameraComponent* getCamera();
private:
    elix::CameraComponent* m_camera;
    float m_movementSpeed{3.0f};

    float m_mouseSensitivity{0.1f};
    bool m_firstClick{true};
};


#endif //CAMERA_HPP
