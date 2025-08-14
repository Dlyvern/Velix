#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "VelixFlow/Components/CameraComponent.hpp"

class Camera
{
public:
    explicit Camera(elix::components::CameraComponent* camera);

    void update(float deltaTime);
    
    elix::components::CameraComponent* getCamera();
private:
    elix::components::CameraComponent* m_camera;
    float m_movementSpeed{3.0f};

    float m_mouseSensitivity{0.1f};
    bool m_firstClick{true};
};


#endif //CAMERA_HPP
