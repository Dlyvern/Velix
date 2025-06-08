#include "CameraManager.hpp"

void CameraManager::setActiveCamera(elix::CameraComponent* camera)
{
    m_activeCamera = camera;
}

elix::CameraComponent* CameraManager::getActiveCamera() const
{
    return m_activeCamera;
}

CameraManager& CameraManager::getInstance()
{
    static CameraManager instance;

    return instance;
}

void CameraManager::addCameraInTheScene(elix::CameraComponent *camera)
{
    m_camerasInTheScene.push_back(camera);
}

elix::CameraComponent* CameraManager::getCameraInTheScene(int index) const
{
    if(index >= m_camerasInTheScene.size() || index < 0)
        return nullptr;

    return m_camerasInTheScene[index];
}

