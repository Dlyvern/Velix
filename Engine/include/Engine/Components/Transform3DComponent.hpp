#ifndef ELIX_TRANSFORM_3D_HPP
#define ELIX_TRANSFORM_3D_HPP

#define GLM_ENABLE_EXPERIMENTAL

#include "Core/Macros.hpp"
#include "Engine/Components/ECS.hpp"
#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Transform3DComponent : public ECS
{
public:
    void setPosition(const glm::vec3& position);
    void setScale(const glm::vec3& scale);
    void setRotation(const glm::quat& quat); //TODO also allow to pass 3d vector

    glm::vec3 getEulerDegrees() const;
    void setEulerDegrees(const glm::vec3& eulerDeg);

    const glm::vec3& getPosition() const;
    glm::vec3& getPosition();
    const glm::vec3& getScale() const;
    glm::vec3& getScale();
    const glm::quat& getRotation() const;
    glm::mat4 getMatrix() const;
private:
    glm::vec3 m_position{0.0f};
    glm::vec3 m_scale{1.0f};
    glm::quat m_rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_TRANSFORM_3D_HPP