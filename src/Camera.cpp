#include "Camera.hpp"
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <ElixirCore/Keyboard.hpp>
#include "ElixirCore/Mouse.hpp"
#include "ElixirCore/WindowsManager.hpp"

Camera::Camera()
{
    updateCameraVectors();
}

void Camera::update(float deltaTime)
{
    if (m_mode == CameraMode::Static)
    {
        updateCameraVectors();
        return;
    }

    static bool mouseLocked{false};

    auto* window = window::WindowsManager::instance().getCurrentWindow();

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
        updateCameraVectors();
        return;
    }

    float velocity = m_movementSpeed * deltaTime;
    auto position = this->getPosition();

    if(input::Keyboard.isKeyPressed(input::KeyCode::W))
        position += getForward() * velocity;

    if(input::Keyboard.isKeyPressed(input::KeyCode::S))
        position -= getForward() * velocity;

    if(input::Keyboard.isKeyPressed(input::KeyCode::A))
        position -= velocity * glm::normalize(glm::cross(getForward(), getUp()));

    if(input::Keyboard.isKeyPressed(input::KeyCode::D))
        position += velocity * glm::normalize(glm::cross(getForward(), getUp()));

    this->setPosition(position);

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

    m_yaw += offsetX;
    m_pitch -= offsetY;

    // if (m_yaw > 180.0f)  m_yaw -= 360.0f;
    // if (m_yaw < -180.0f) m_yaw += 360.0f;

    m_pitch = glm::clamp(m_pitch - offsetY, -89.0f, 89.0f);

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

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const
{
    return glm::perspective(glm::radians(60.0f), aspectRatio, 0.1f, 1000.0f);
}

void Camera::setPosition(const glm::vec3 &position)
{
    m_position = position;
}

void Camera::setCameraMode(const CameraMode &mode)
{
    m_mode = mode;
}
