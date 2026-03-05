#include "Engine/Camera.hpp"

#include <algorithm>

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

float Camera::getAspect() const
{
    return m_aspect;
}

Camera::ProjectionMode Camera::getProjectionMode() const
{
    return m_projectionMode;
}

float Camera::getOrthographicSize() const
{
    return m_orthographicSize;
}

glm::mat4 Camera::getProjectionMatrix() const
{
    if (m_projectionMode == ProjectionMode::Orthographic)
    {
        const float halfHeight = std::max(m_orthographicSize * 0.5f, 0.001f);
        const float halfWidth = std::max(halfHeight * m_aspect, 0.001f);
        return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, m_near, m_far);
    }

    return glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
}

void Camera::setYaw(float yaw)
{
    m_yaw = yaw;
    updateCameraVectors();
}

void Camera::setFOV(float fov)
{
    m_fov = glm::clamp(fov, 1.0f, 179.0f);
}

void Camera::setAspect(float aspect)
{
    m_aspect = std::max(aspect, 0.001f);
}

void Camera::setNear(float nearPlane)
{
    m_near = std::max(nearPlane, 0.001f);
    if (m_far <= m_near)
        m_far = m_near + 0.001f;
}

void Camera::setFar(float farPlane)
{
    m_far = std::max(farPlane, m_near + 0.001f);
}

void Camera::setProjectionMode(ProjectionMode mode)
{
    m_projectionMode = mode;
}

void Camera::setOrthographicSize(float size)
{
    m_orthographicSize = std::max(size, 0.001f);
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
