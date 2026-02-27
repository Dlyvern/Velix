#include "Engine/Components/CollisionComponent.hpp"
#include "Engine/Entity.hpp"
#include "Engine/Components/Transform3DComponent.hpp"

#include <glm/common.hpp>
#include <algorithm>
#include <iostream>
#include <geometry/PxGeometryHelpers.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

CollisionComponent::CollisionComponent(
    physx::PxShape *shape,
    ShapeType shapeType,
    const glm::vec3 &boxHalfExtents,
    float capsuleRadius,
    float capsuleHalfHeight,
    physx::PxRigidStatic *actor)
    : m_shape(shape),
      m_shapeType(shapeType),
      m_boxHalfExtents(boxHalfExtents),
      m_capsuleRadius(capsuleRadius),
      m_capsuleHalfHeight(capsuleHalfHeight),
      m_actor(actor)
{
    cacheGeometryFromShape();
}

void CollisionComponent::update(float deltaTime)
{
    (void)deltaTime;

    if (!m_actor)
        return;

    auto owner = getOwner<Entity>();

    if (!owner)
    {
        VX_ENGINE_ERROR_STREAM("Failed to get owner for rigid body component\n");
        return;
    }

    auto *transormComponent = owner->getComponent<Transform3DComponent>();

    if (!transormComponent)
    {
        VX_ENGINE_ERROR_STREAM("Failed to get transform component\n");
        return;
    }

    physx::PxTransform transform = m_actor->getGlobalPose();
    const auto worldPosition = transormComponent->getWorldPosition();
    const auto worldRotation = transormComponent->getWorldRotation();
    transform.p = physx::PxVec3(worldPosition.x, worldPosition.y, worldPosition.z);
    transform.q = physx::PxQuat(worldRotation.x, worldRotation.y, worldRotation.z, worldRotation.w);

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

CollisionComponent::ShapeType CollisionComponent::getShapeType() const
{
    return m_shapeType;
}

const glm::vec3 &CollisionComponent::getBoxHalfExtents() const
{
    return m_boxHalfExtents;
}

float CollisionComponent::getCapsuleRadius() const
{
    return m_capsuleRadius;
}

float CollisionComponent::getCapsuleHalfHeight() const
{
    return m_capsuleHalfHeight;
}

bool CollisionComponent::setBoxHalfExtents(const glm::vec3 &halfExtents)
{
    if (!m_shape || m_shapeType != ShapeType::BOX)
        return false;

    const glm::vec3 clampedHalfExtents = glm::max(halfExtents, glm::vec3(0.01f));
    const physx::PxBoxGeometry geometry(clampedHalfExtents.x, clampedHalfExtents.y, clampedHalfExtents.z);
    m_shape->setGeometry(geometry);

    m_boxHalfExtents = clampedHalfExtents;
    return true;
}

bool CollisionComponent::setCapsuleDimensions(float radius, float halfHeight)
{
    if (!m_shape || m_shapeType != ShapeType::CAPSULE)
        return false;

    const float clampedRadius = std::max(radius, 0.01f);
    const float clampedHalfHeight = std::max(halfHeight, 0.0f);
    const physx::PxCapsuleGeometry geometry(clampedRadius, clampedHalfHeight);
    m_shape->setGeometry(geometry);

    m_capsuleRadius = clampedRadius;
    m_capsuleHalfHeight = clampedHalfHeight;
    return true;
}

physx::PxShape *CollisionComponent::getShape()
{
    return m_shape;
}

void CollisionComponent::cacheGeometryFromShape()
{
    if (!m_shape)
        return;

    const physx::PxGeometryHolder geometryHolder(m_shape->getGeometry());
    const physx::PxGeometryType::Enum geometryType = geometryHolder.getType();
    if (geometryType == physx::PxGeometryType::eBOX)
    {
        const auto &geometry = geometryHolder.box();
        m_shapeType = ShapeType::BOX;
        m_boxHalfExtents = glm::vec3(geometry.halfExtents.x, geometry.halfExtents.y, geometry.halfExtents.z);
    }
    else if (geometryType == physx::PxGeometryType::eCAPSULE)
    {
        const auto &geometry = geometryHolder.capsule();
        m_shapeType = ShapeType::CAPSULE;
        m_capsuleRadius = geometry.radius;
        m_capsuleHalfHeight = geometry.halfHeight;
    }
}

ELIX_NESTED_NAMESPACE_END
