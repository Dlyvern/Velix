#include "Engine/Camera.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Camera::Camera()
{
    updateCameraVectors();
}

glm::vec3 Camera::getPosition() const
{
    return m_position;
}

glm::vec3 Camera::getForward() const
{
    return m_forward;
}

glm::vec3 Camera::getUp() const
{
    return m_up;
}

glm::mat4 Camera::getViewMatrix() const
{
    return glm::lookAt(m_position, m_position + m_forward, m_up);
}

float Camera::getPitch() const
{
    return m_pitch;
}

float Camera::getYaw() const
{
    return m_yaw;
}

float Camera::getFOV() const 
{
    return m_fov;
}

float Camera::getNear() const 
{
    return m_near;
};

float Camera::getFar() const 
{
    return m_far;
};

glm::mat4 Camera::getProjectionMatrix() const
{
    return glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
}

void Camera::setYaw(float yaw)
{
    m_yaw = yaw;
    updateCameraVectors();
}

void Camera::setPosition(const glm::vec3 &position)
{
    m_position = position;
}

void Camera::setPitch(float pitch)
{
    m_pitch = glm::clamp(pitch, -89.0f, 89.0f);
    updateCameraVectors();
}

void Camera::updateCameraVectors()
{
    glm::vec3 forward;
    forward.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    forward.y = sin(glm::radians(m_pitch));
    forward.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));

    m_forward = glm::normalize(forward);
    m_right = glm::normalize(glm::cross(m_forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    m_up = glm::normalize(glm::cross(m_right, m_forward));
}

ELIX_NESTED_NAMESPACE_END

// #include "Camera.hpp"
// #include <glm/ext/matrix_clip_space.hpp>
// #include <glm/ext/matrix_transform.hpp>

// #include <VelixFlow/Input/Keyboard.hpp>
// #include "VelixFlow/Input/Mouse.hpp"
// #include "Engine.hpp"

// Camera::Camera(elix::components::CameraComponent* camera) : m_camera(camera) {}

// void Camera::update(float deltaTime)
// {
//     static bool mouseLocked{false};

//       const window::Window* window = Engine::s_window;

//     if (input::Mouse.isRightButtonPressed() && !mouseLocked)
//     {
//         // glfwSetInputMode(window->getGLFWWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED); 
//         m_firstClick = true;
//         mouseLocked = true;
//     }
//     else if((!input::Mouse.isRightButtonPressed()) && mouseLocked)
//     {
//         // glfwSetInputMode(window->getGLFWWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
//         mouseLocked = false;
//     }

//     if (!mouseLocked)
//     {
//         m_camera->update(deltaTime);
//         return;
//     }

//     const float velocity = m_movementSpeed * deltaTime;
//     auto position = m_camera->getPosition();
//     const auto forward = m_camera->getForward();
//     const auto up = m_camera->getUp();

//     if(input::Keyboard.isKeyPressed(input::KeyCode::W))
//         position += forward * velocity;

//     if(input::Keyboard.isKeyPressed(input::KeyCode::S))
//         position -= forward * velocity;

//     if(input::Keyboard.isKeyPressed(input::KeyCode::A))
//         position -= velocity * glm::normalize(glm::cross(forward, up));

//     if(input::Keyboard.isKeyPressed(input::KeyCode::D))
//         position += velocity * glm::normalize(glm::cross(forward, up));

//     m_camera->setPosition(position);

//     static float lastX = static_cast<float>(window->getWidth()) / 2.0f;
//     static float lastY = static_cast<float>(window->getHeight()) / 2.0f;

//     const auto xPosition = static_cast<float>(input::Mouse.getX());
//     const auto yPosition = static_cast<float>(input::Mouse.getY());

//     if(m_firstClick)
//     {
//         lastX = xPosition;
//         lastY = yPosition;
//         m_firstClick = false;
//     }

//     float offsetX = xPosition - lastX;
//     float offsetY = yPosition - lastY;

//     lastX = xPosition;
//     lastY = yPosition;

//     offsetX *= m_mouseSensitivity;
//     offsetY *= m_mouseSensitivity;

//     float yaw = m_camera->getYaw();
//     float pitch = m_camera->getPitch();

//     yaw += offsetX;
//     pitch -= offsetY;

//     m_camera->setYaw(yaw);
//     m_camera->setPitch(pitch);

//     m_camera->update(deltaTime);

// }

// elix::components::CameraComponent* Camera::getCamera()
// {
//     return m_camera;
// }
