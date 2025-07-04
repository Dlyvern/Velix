#include "Camera.hpp"
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <ElixirCore/Keyboard.hpp>
#include "ElixirCore/Mouse.hpp"
#include "Engine.hpp"

Camera::Camera(elix::CameraComponent* camera) : m_camera(camera) {}

void Camera::update(float deltaTime)
{
    static bool mouseLocked{false};

    const auto* window = Engine::s_application->getWindow();

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
        m_camera->update(deltaTime);
        return;
    }

    const float velocity = m_movementSpeed * deltaTime;
    auto position = m_camera->getPosition();
    const auto forward = m_camera->getForward();
    const auto up = m_camera->getUp();

    if(input::Keyboard.isKeyPressed(input::KeyCode::W))
        position += forward * velocity;

    if(input::Keyboard.isKeyPressed(input::KeyCode::S))
        position -= forward * velocity;

    if(input::Keyboard.isKeyPressed(input::KeyCode::A))
        position -= velocity * glm::normalize(glm::cross(forward, up));

    if(input::Keyboard.isKeyPressed(input::KeyCode::D))
        position += velocity * glm::normalize(glm::cross(forward, up));

    m_camera->setPosition(position);

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

    float yaw = m_camera->getYaw();
    float pitch = m_camera->getPitch();

    yaw += offsetX;
    pitch -= offsetY;

    m_camera->setYaw(yaw);
    m_camera->setPitch(pitch);

    m_camera->update(deltaTime);
}

elix::CameraComponent* Camera::getCamera()
{
    return m_camera;
}
