#include "Engine/Components/CharacterMovementComponent.hpp"

#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Entity.hpp"
#include "Engine/Scene.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

CharacterMovementComponent::CharacterMovementComponent(Scene *scene,
                                                       float capsuleRadius,
                                                       float capsuleHeight)
    : m_scene(scene),
      m_capsuleRadius(std::max(capsuleRadius, 0.05f)),
      m_capsuleHeight(std::max(capsuleHeight, 0.1f))
{
}

CharacterMovementComponent::~CharacterMovementComponent()
{
    releaseController();
}

void CharacterMovementComponent::update(float deltaTime)
{
    (void)deltaTime;

    if (!ensureController() || !m_transformComponent)
        return;

    const glm::vec3 transformPosition = m_transformComponent->getWorldPosition();
    if (m_hasLastTransformPosition && glm::distance2(transformPosition, m_lastTransformPosition) > 1e-6f)
    {
        m_controller->setPosition(physx::PxExtendedVec3(transformPosition.x, transformPosition.y, transformPosition.z));
    }

    syncTransformFromController();
}

void CharacterMovementComponent::onDetach()
{
    ECS::onDetach();
    releaseController();
}

bool CharacterMovementComponent::move(const glm::vec3 &worldDisplacement, float deltaTime, float minDistance)
{
    if (!ensureController())
        return false;

    const float safeDeltaTime = std::max(deltaTime, 1e-6f);
    const float safeMinDistance = std::max(minDistance, 0.0f);

    m_lastCollisionFlags = m_controller->move(
        physx::PxVec3(worldDisplacement.x, worldDisplacement.y, worldDisplacement.z),
        safeMinDistance,
        safeDeltaTime,
        physx::PxControllerFilters());

    syncTransformFromController();
    return true;
}

void CharacterMovementComponent::teleport(const glm::vec3 &worldPosition)
{
    if (!ensureController())
        return;

    m_controller->setPosition(physx::PxExtendedVec3(worldPosition.x, worldPosition.y, worldPosition.z));
    syncTransformFromController();
}

bool CharacterMovementComponent::setCapsule(float radius, float height)
{
    const float clampedRadius = std::max(radius, 0.05f);
    const float clampedHeight = std::max(height, 0.1f);

    if (m_capsuleRadius == clampedRadius && m_capsuleHeight == clampedHeight)
        return true;

    m_capsuleRadius = clampedRadius;
    m_capsuleHeight = clampedHeight;

    if (!m_controller)
        return true;

    if (!m_scene)
        return false;

    const physx::PxExtendedVec3 controllerPosition = m_controller->getPosition();
    releaseController();

    m_controller = m_scene->getPhysicsScene().createController(
        physx::PxVec3(
            static_cast<float>(controllerPosition.x),
            static_cast<float>(controllerPosition.y),
            static_cast<float>(controllerPosition.z)),
        m_capsuleRadius,
        m_capsuleHeight);
    if (!m_controller)
        return false;

    applyControllerParameters();
    syncTransformFromController();
    return true;
}

float CharacterMovementComponent::getCapsuleRadius() const
{
    return m_capsuleRadius;
}

float CharacterMovementComponent::getCapsuleHeight() const
{
    return m_capsuleHeight;
}

void CharacterMovementComponent::setStepOffset(float stepOffset)
{
    m_stepOffset = std::max(stepOffset, 0.0f);
    if (m_controller)
        m_controller->setStepOffset(m_stepOffset);
}

float CharacterMovementComponent::getStepOffset() const
{
    return m_stepOffset;
}

void CharacterMovementComponent::setContactOffset(float contactOffset)
{
    m_contactOffset = std::max(contactOffset, 0.001f);
    if (m_controller)
        m_controller->setContactOffset(m_contactOffset);
}

float CharacterMovementComponent::getContactOffset() const
{
    return m_contactOffset;
}

void CharacterMovementComponent::setSlopeLimitDegrees(float slopeLimitDegrees)
{
    m_slopeLimitDegrees = std::clamp(slopeLimitDegrees, 0.0f, 89.0f);
    if (!m_controller)
        return;

    const float slopeCosine = std::cos(glm::radians(m_slopeLimitDegrees));
    m_controller->setSlopeLimit(slopeCosine);
}

float CharacterMovementComponent::getSlopeLimitDegrees() const
{
    return m_slopeLimitDegrees;
}

bool CharacterMovementComponent::hasController() const
{
    return m_controller != nullptr;
}

bool CharacterMovementComponent::isGrounded() const
{
    return m_lastCollisionFlags.isSet(physx::PxControllerCollisionFlag::eCOLLISION_DOWN);
}

uint32_t CharacterMovementComponent::getCollisionFlags() const
{
    return static_cast<uint32_t>(m_lastCollisionFlags);
}

void CharacterMovementComponent::onOwnerAttached()
{
    ECS::onOwnerAttached();

    auto *owner = getOwner<Entity>();
    m_transformComponent = owner ? owner->getComponent<Transform3DComponent>() : nullptr;
    ensureController();
}

bool CharacterMovementComponent::ensureController()
{
    if (m_controller)
        return true;

    if (!m_scene || !m_transformComponent)
        return false;

    const glm::vec3 worldPosition = m_transformComponent->getWorldPosition();
    m_controller = m_scene->getPhysicsScene().createController(
        physx::PxVec3(worldPosition.x, worldPosition.y, worldPosition.z),
        m_capsuleRadius,
        m_capsuleHeight);

    if (!m_controller)
        return false;

    applyControllerParameters();
    syncTransformFromController();
    return true;
}

void CharacterMovementComponent::releaseController()
{
    if (!m_controller)
        return;

    m_controller->release();
    m_controller = nullptr;
}

void CharacterMovementComponent::syncTransformFromController()
{
    if (!m_controller || !m_transformComponent)
        return;

    const physx::PxExtendedVec3 controllerPosition = m_controller->getPosition();
    const glm::vec3 worldPosition(
        static_cast<float>(controllerPosition.x),
        static_cast<float>(controllerPosition.y),
        static_cast<float>(controllerPosition.z));

    m_transformComponent->setWorldPosition(worldPosition);
    m_lastTransformPosition = worldPosition;
    m_hasLastTransformPosition = true;
}

void CharacterMovementComponent::applyControllerParameters()
{
    if (!m_controller)
        return;

    m_controller->setStepOffset(m_stepOffset);
    m_controller->setContactOffset(std::max(m_contactOffset, 0.001f));
    const float slopeCosine = std::cos(glm::radians(std::clamp(m_slopeLimitDegrees, 0.0f, 89.0f)));
    m_controller->setSlopeLimit(slopeCosine);
}

ELIX_NESTED_NAMESPACE_END
