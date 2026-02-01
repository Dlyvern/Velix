#include "Engine/Components/CollisionComponent.hpp"
#include "Engine/Entity.hpp"
#include "Engine/Components/Transform3DComponent.hpp"

#include <iostream>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

CollisionComponent::CollisionComponent(physx::PxShape *shape, physx::PxRigidStatic *actor) : m_shape(shape), m_actor(actor)
{
}

void CollisionComponent::update(float deltaTime)
{
    if (!m_actor)
        return;

    auto owner = getOwner<Entity>();

    if (!owner)
    {
        std::cerr << "Failed to get owner for rigid body component\n";
        return;
    }

    auto *transormComponent = owner->getComponent<Transform3DComponent>();

    if (!transormComponent)
    {
        std::cerr << "Failed to get transform component\n";
        return;
    }

    physx::PxTransform transform = m_actor->getGlobalPose();
    // transform.p = physx::PxVec3(position.x, position.y, position.z);
    transform.p = physx::PxVec3(transormComponent->getPosition().x, transormComponent->getPosition().y, transormComponent->getPosition().z);

    m_actor->setGlobalPose(transform);
}

void CollisionComponent::removeActor()
{
    m_actor = nullptr;
}

physx::PxRigidStatic *CollisionComponent::getActor()
{
    return m_actor;
}

physx::PxShape *CollisionComponent::getShape()
{
    return m_shape;
}

ELIX_NESTED_NAMESPACE_END