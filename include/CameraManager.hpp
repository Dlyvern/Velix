#ifndef CAMERA_MANAGER_HPP
#define CAMERA_MANAGER_HPP

#include "Camera.hpp"
#include <vector>

class CameraManager
{
public:
   void setActiveCamera(elix::CameraComponent* camera);
   [[nodiscard]] elix::CameraComponent* getActiveCamera() const;
   static CameraManager& getInstance();

   void addCameraInTheScene(elix::CameraComponent* camera);

   [[nodiscard]] elix::CameraComponent* getCameraInTheScene(int index) const;
private:
   Camera* m_editorCamera{nullptr};


   elix::CameraComponent* m_activeCamera{nullptr};
   std::vector<elix::CameraComponent*> m_camerasInTheScene;
};

#endif //CAMERA_MANAGER_HPP
