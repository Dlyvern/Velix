#include "Engine/Components/RigidBodyComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Entity.hpp"

#include <iostream>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

RigidBodyComponent::RigidBodyComponent(physx::PxRigidActor *rigidActor) : m_rigidActor(rigidActor)
{
}

void RigidBodyComponent::update(float deltaTime)
{
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

    transormComponent->setPosition(glm::vec3(
        m_rigidActor->getGlobalPose().p.x, m_rigidActor->getGlobalPose().p.y, m_rigidActor->getGlobalPose().p.z));
}

void RigidBodyComponent::setKinematic(bool isKinematic)
{
    if (m_rigidActor->is<physx::PxRigidDynamic>())
    {
        m_rigidActor->is<physx::PxRigidDynamic>()->setRigidBodyFlag(
            physx::PxRigidBodyFlag::eKINEMATIC, isKinematic);
        // isKinematic = isKinematic;
    }
}

void RigidBodyComponent::setPosition(const glm::vec3 &vec)
{
    setPosition(physx::PxVec3(vec.x, vec.y, vec.z));
}

void RigidBodyComponent::setPosition(const physx::PxVec3 &vec)
{
    physx::PxTransform transform = m_rigidActor->getGlobalPose();
    // transform.p = physx::PxVec3(position.x, position.y, position.z);
    transform.p = vec;

    m_rigidActor->setGlobalPose(transform);
}

void RigidBodyComponent::setGravityEnable(bool enable)
{
    m_rigidActor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !enable);
}

physx::PxRigidActor *RigidBodyComponent::getRigidActor() const
{
    return m_rigidActor;
}

ELIX_NESTED_NAMESPACE_END