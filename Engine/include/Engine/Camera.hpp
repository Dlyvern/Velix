#ifndef ELIX_CAMERA_HPP
#define ELIX_CAMERA_HPP

#include "Core/Macros.hpp"

#include <glm/mat4x4.hpp>

#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Camera
{
public:
    using SharedPtr = std::shared_ptr<Camera>;
    Camera();

    [[nodiscard]] glm::vec3 getPosition() const;
    [[nodiscard]] glm::vec3 getForward() const;
    [[nodiscard]] glm::vec3 getUp() const;
    [[nodiscard]] glm::mat4 getViewMatrix() const;
    [[nodiscard]] float getPitch() const;
    [[nodiscard]] float getYaw() const;
    [[nodiscard]] glm::mat4 getProjectionMatrix() const;

    [[nodiscard]] float getFOV() const;
    [[nodiscard]] float getNear() const;
    [[nodiscard]] float getFar() const;

    void setYaw(float yaw);
    void setPitch(float pitch);
    void setPosition(const glm::vec3& position);

    void updateCameraVectors();

    virtual ~Camera() = default;
private:
    glm::vec3 m_position{2.0f, 2.0f, 2.0f};
    glm::vec3 m_up{glm::vec3(0.0f, 1.0f, 0.0f)};
    glm::vec3 m_right{glm::vec3(0.0f, 1.0f, 0.0f)};
    glm::vec3 m_forward{glm::vec3(0.0f, 0.0f, -1.0f)};

    float m_yaw{-90.0f};
    float m_pitch{0.0f};

    float m_fov{60.0f};
    float m_aspect{1.77f};
    float m_near{0.1f};
    float m_far{1000.0f};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_CAMERA_HPP




// #ifndef CAMERA_HPP
// #define CAMERA_HPP

// #include "VelixFlow/Components/CameraComponent.hpp"

// class Camera
// {
// public:
//     explicit Camera(elix::components::CameraComponent* camera);

//     void update(float deltaTime);
    
//     elix::components::CameraComponent* getCamera();
// private:
//     elix::components::CameraComponent* m_camera;
//     float m_movementSpeed{3.0f};

//     float m_mouseSensitivity{0.1f};
//     bool m_firstClick{true};
// };


// #endif //CAMERA_HPP
