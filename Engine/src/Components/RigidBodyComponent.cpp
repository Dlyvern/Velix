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
    (void)deltaTime;
    syncToPhysics();
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

bool RigidBodyComponent::isKinematic() const
{
    if (!m_rigidActor)
        return false;

    auto *dynamicBody = m_rigidActor->is<physx::PxRigidDynamic>();
    if (!dynamicBody)
        return false;

    return dynamicBody->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC);
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

void RigidBodyComponent::onOwnerAttached()
{
    ECS::onOwnerAttached();

    auto *owner = getOwner<Entity>();
    m_transformComponent = owner ? owner->getComponent<Transform3DComponent>() : nullptr;
}

void RigidBodyComponent::syncToPhysics()
{
    if (!m_rigidActor || !m_transformComponent || !isKinematic())
        return;

    const auto position = m_transformComponent->getWorldPosition();
    const auto rotation = m_transformComponent->getWorldRotation();

    m_rigidActor->setGlobalPose(physx::PxTransform(
        physx::PxVec3(position.x, position.y, position.z),
        physx::PxQuat(rotation.x, rotation.y, rotation.z, rotation.w)));
}

void RigidBodyComponent::syncFromPhysics()
{
    if (!m_rigidActor || !m_transformComponent)
        return;

    const physx::PxTransform pose = m_rigidActor->getGlobalPose();

    m_transformComponent->setWorldPosition(glm::vec3(pose.p.x, pose.p.y, pose.p.z));
    m_transformComponent->setWorldRotation(glm::quat(pose.q.w, pose.q.x, pose.q.y, pose.q.z));
}

physx::PxRigidActor *RigidBodyComponent::getRigidActor() const
{
    return m_rigidActor;
}

ELIX_NESTED_NAMESPACE_END
