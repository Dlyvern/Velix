#ifndef CAMERA_MANAGER_HPP
#define CAMERA_MANAGER_HPP

#include "Camera.hpp"

class CameraManager
{
public:
  void setActiveCamera(Camera* camera);
  [[nodiscard]] Camera* getActiveCamera() const;
  static CameraManager& getInstance();
  Camera* createCamera();
private:
   Camera* m_activeCamera{nullptr};
};

#endif //CAMERA_MANAGER_HPP
