#ifndef ELIX_CAMERA_COMPONENT_HPP
#define ELIX_CAMERA_COMPONENT_HPP

#include "Core/Macros.hpp"
#include "Engine/Components/ECS.hpp"
#include "Engine/Camera.hpp"

#include <glm/vec3.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class CameraComponent : public ECS
{
public:
    explicit CameraComponent(Camera::SharedPtr camera);
    CameraComponent();

    Camera::SharedPtr getCamera() const;
    void update(float deltaTime) override;

    void syncFromOwnerTransform() const;

    void setPositionOffset(const glm::vec3 &offset);
    const glm::vec3 &getPositionOffset() const;

protected:
    void onOwnerAttached() override;

private:
    Camera::SharedPtr m_camera{nullptr};
    glm::vec3 m_positionOffset{0.0f, 0.0f, 0.0f};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_CAMERA_COMPONENT_HPP
