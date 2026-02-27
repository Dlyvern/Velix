#ifndef ELIX_PHYSICS_SCENE_HPP
#define ELIX_PHYSICS_SCENE_HPP

#include "Engine/Physics/PhysXCore.hpp"

#include <glm/vec3.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct PhysicsRaycastHit
{
    void *actorUserData{nullptr};
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    float distance{0.0f};
};

class PhysicsScene
{
public:
    explicit PhysicsScene(physx::PxPhysics *physics);

    void update(float deltaTime);

    bool raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance, PhysicsRaycastHit *outHit = nullptr) const;

    physx::PxRigidDynamic *createDynamic(const physx::PxTransform &transform);

    physx::PxShape *createShape(const physx::PxGeometry &geometry);

    physx::PxRigidStatic *createStatic(const physx::PxTransform &transform);

    void removeActor(physx::PxActor &actor, bool wakeOnLostTouch = true, bool release = false);

    physx::PxController *createController(const physx::PxVec3 &position, float radius, float height);

    ~PhysicsScene();

private:
    physx::PxPhysics *m_physics = nullptr;

    physx::PxDefaultCpuDispatcher *m_defaultCpuDispatcher = nullptr;
    physx::PxMaterial *m_defaultMaterial = nullptr;
    physx::PxScene *m_scene = nullptr;
    physx::PxControllerManager *m_controllerManager = nullptr;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PHYSICS_SCENE_HPP
