#ifndef ELIX_CHARACTER_MOVEMENT_COMPONENT_HPP
#define ELIX_CHARACTER_MOVEMENT_COMPONENT_HPP

#include "Engine/Components/ECS.hpp"
#include "Engine/Physics/PhysXCore.hpp"

#include <glm/vec3.hpp>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Scene;
class Transform3DComponent;

class CharacterMovementComponent : public ECS
{
public:
    CharacterMovementComponent(Scene *scene,
                               float capsuleRadius = 0.35f,
                               float capsuleHeight = 1.0f);
    ~CharacterMovementComponent() override;

    void update(float deltaTime) override;
    void onDetach() override;

    bool move(const glm::vec3 &worldDisplacement, float deltaTime, float minDistance = 0.001f);
    void teleport(const glm::vec3 &worldPosition);

    bool setCapsule(float radius, float height);
    float getCapsuleRadius() const;
    float getCapsuleHeight() const;
    void setCapsuleCenterOffsetY(float offsetY);
    float getCapsuleCenterOffsetY() const;

    void setStepOffset(float stepOffset);
    float getStepOffset() const;

    void setContactOffset(float contactOffset);
    float getContactOffset() const;

    void setSlopeLimitDegrees(float slopeLimitDegrees);
    float getSlopeLimitDegrees() const;

    bool hasController() const;
    bool isGrounded() const;
    uint32_t getCollisionFlags() const;

protected:
    void onOwnerAttached() override;

private:
    bool ensureController();
    void releaseController();
    void syncTransformFromController();
    void applyControllerParameters();
    void syncControllerUserData();
    glm::vec3 getControllerWorldPositionForTransform(const glm::vec3 &transformWorldPosition) const;
    glm::vec3 getTransformWorldPositionForController(const physx::PxExtendedVec3 &controllerPosition) const;

    Scene *m_scene{nullptr};
    Transform3DComponent *m_transformComponent{nullptr};
    physx::PxController *m_controller{nullptr};

    float m_capsuleRadius{0.35f};
    float m_capsuleHeight{1.0f};
    float m_capsuleCenterOffsetY{0.0f};
    float m_stepOffset{0.3f};
    float m_contactOffset{0.05f};
    float m_slopeLimitDegrees{45.0f};

    bool m_hasLastTransformPosition{false};
    glm::vec3 m_lastTransformPosition{0.0f};
    physx::PxControllerCollisionFlags m_lastCollisionFlags{};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_CHARACTER_MOVEMENT_COMPONENT_HPP
