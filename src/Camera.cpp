#include "Camera.hpp"
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <ElixirCore/Keyboard.hpp>
#include "ElixirCore/Mouse.hpp"
#include "ElixirCore/WindowsManager.hpp"

Camera::Camera() = default;

void Camera::update(float deltaTime)
{
    // if (m_mode == CameraMode::Static)
    //     return m_camera.update(deltaTime);

    static bool mouseLocked{false};

    const auto* window = window::WindowsManager::instance().getCurrentWindow();

    if (input::Mouse.isRightButtonPressed() && !mouseLocked)
    {
        glfwSetInputMode(window->getOpenGLWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        m_firstClick = true;
        mouseLocked = true;
    }
    else if((!input::Mouse.isRightButtonPressed()) && mouseLocked)
    {
        glfwSetInputMode(window->getOpenGLWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        mouseLocked = false;
    }

    if (!mouseLocked)
    {
        m_camera.update(deltaTime);
        return;
    }

    const float velocity = m_movementSpeed * deltaTime;
    auto position = m_camera.getPosition();
    const auto forward = m_camera.getForward();
    const auto up = m_camera.getUp();

    if(input::Keyboard.isKeyPressed(input::KeyCode::W))
        position += forward * velocity;

    if(input::Keyboard.isKeyPressed(input::KeyCode::S))
        position -= forward * velocity;

    if(input::Keyboard.isKeyPressed(input::KeyCode::A))
        position -= velocity * glm::normalize(glm::cross(forward, up));

    if(input::Keyboard.isKeyPressed(input::KeyCode::D))
        position += velocity * glm::normalize(glm::cross(forward, up));

    m_camera.setPosition(position);

    static float lastX = static_cast<float>(window->getWidth()) / 2.0f;
    static float lastY = static_cast<float>(window->getHeight()) / 2.0f;

    const auto xPosition = static_cast<float>(input::Mouse.getX());
    const auto yPosition = static_cast<float>(input::Mouse.getY());

    if(m_firstClick)
    {
        lastX = xPosition;
        lastY = yPosition;
        m_firstClick = false;
    }

    float offsetX = xPosition - lastX;
    float offsetY = yPosition - lastY;

    lastX = xPosition;
    lastY = yPosition;

    offsetX *= m_mouseSensitivity;
    offsetY *= m_mouseSensitivity;

    float yaw = m_camera.getYaw();
    float pitch = m_camera.getPitch();

    yaw += offsetX;
    pitch -= offsetY;

    m_camera.setYaw(yaw);
    m_camera.setPitch(pitch);

    m_camera.update(deltaTime);
}

glm::vec3 Camera::getPosition() const
{
    return m_camera.getPosition();
}

glm::mat4 Camera::getViewMatrix() const
{
    return m_camera.getViewMatrix();
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const
{
    return glm::perspective(glm::radians(60.0f), aspectRatio, 0.1f, 1000.0f);
}

void Camera::setCameraMode(const CameraMode &mode)
{
    m_mode = mode;
}

elix::CameraComponent* Camera::getCamera()
{
    return &m_camera;
}
