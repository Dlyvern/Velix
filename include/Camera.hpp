#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include "ElixirCore/CameraComponent.hpp"

enum class CameraMode : uint8_t
{
    Static = 0,
    FPS = 1
};

class Camera
{
public:
    Camera();
    void update(float deltaTime);
    
    glm::vec3 getPosition() const;
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspectRatio) const;
    void setCameraMode(const CameraMode& mode);


    elix::CameraComponent* getCamera();

private:
    elix::CameraComponent m_camera;
    float m_movementSpeed{3.0f};

    CameraMode m_mode = CameraMode::Static;

    float m_mouseSensitivity{0.1f};
    bool m_firstClick{true};
};


#endif //CAMERA_HPP
