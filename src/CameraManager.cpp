#include "CameraManager.hpp"

void CameraManager::setActiveCamera(Camera* camera)
{
    m_activeCamera = camera;
}

Camera* CameraManager::getActiveCamera() const
{
    return m_activeCamera;
}

CameraManager& CameraManager::getInstance()
{
    static CameraManager instance;

    return instance;
}

//TODO: MEMORY LEAK
Camera* CameraManager::createCamera()
{
    return new Camera();
}

