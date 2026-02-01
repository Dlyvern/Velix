#ifndef ELIX_COLLISION_COMPONENT_HPP
#define ELIX_COLLISION_COMPONENT_HPP

#include "Engine/Components/ECS.hpp"

#include "Engine/Physics/PhysXCore.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class CollisionComponent : public ECS
{
public:
    explicit CollisionComponent(physx::PxShape *shape, physx::PxRigidStatic *actor = nullptr);

    void update(float deltaTime) override;

    physx::PxShape *getShape();
    physx::PxRigidStatic *getActor();

    void removeActor();

private:
    physx::PxShape *m_shape{nullptr};
    physx::PxRigidStatic *m_actor{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_COLLISION_COMPONENT_HPP