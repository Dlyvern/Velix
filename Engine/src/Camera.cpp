#include "Engine/Camera.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
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