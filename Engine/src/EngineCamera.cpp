#include "Engine/EngineCamera.hpp"

#include "Core/VulkanContext.hpp"

#include <GLFW/glfw3.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

EngineCamera::EngineCamera(Camera::SharedPtr camera) : m_camera(camera)
{

}

void EngineCamera::update(float deltaTime)
{
    static bool mouseLocked{false};

    const auto& window = core::VulkanContext::getContext()->getSwapchain()->getWindow();

    int rightButtonState = glfwGetMouseButton(window->getRawHandler(), GLFW_MOUSE_BUTTON_RIGHT);

    if (rightButtonState == GLFW_PRESS && !mouseLocked)
    {
        glfwSetInputMode(window->getRawHandler(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        m_firstClick = true;
        mouseLocked = true;
    }
    else if(rightButtonState == GLFW_RELEASE && mouseLocked)
    {
        glfwSetInputMode(window->getRawHandler(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        mouseLocked = false;
    }

    if (!mouseLocked)
    {
        m_camera->updateCameraVectors();
        return;
    }

    const float velocity = m_movementSpeed * deltaTime;
    auto position = m_camera->getPosition();
    const auto forward = m_camera->getForward();
    const auto up = m_camera->getUp();

    if (glfwGetKey(window->getRawHandler(), GLFW_KEY_W) == GLFW_PRESS)   
        position += forward * velocity;
    if(glfwGetKey(window->getRawHandler(), GLFW_KEY_S) == GLFW_PRESS)
        position -= forward * velocity;

    if(glfwGetKey(window->getRawHandler(), GLFW_KEY_A) == GLFW_PRESS)
        position -= velocity * glm::normalize(glm::cross(forward, up));

    if(glfwGetKey(window->getRawHandler(), GLFW_KEY_D) == GLFW_PRESS)
        position += velocity * glm::normalize(glm::cross(forward, up));

    m_camera->setPosition(position);

    static float lastX = static_cast<float>(core::VulkanContext::getContext()->getSwapchain()->getExtent().width) / 2.0f;
    static float lastY = static_cast<float>(core::VulkanContext::getContext()->getSwapchain()->getExtent().height) / 2.0f;

    double xPosition, yPosition;
    glfwGetCursorPos(window->getRawHandler(), &xPosition, &yPosition);

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

    m_camera->updateCameraVectors();
}

ELIX_NESTED_NAMESPACE_END
