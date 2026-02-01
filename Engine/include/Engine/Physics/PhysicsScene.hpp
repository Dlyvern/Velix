#ifndef ELIX_PHYSICS_SCENE_HPP
#define ELIX_PHYSICS_SCENE_HPP

#include "Engine/Physics/PhysXCore.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class PhysicsScene
{
public:
    explicit PhysicsScene(physx::PxPhysics *physics);

    void update(float deltaTime);

    void raycast();

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