#include "Engine/SDK/VXCharacter.hpp"
#include "Engine/SDK/VXGameState.hpp"
#include "Core/Logger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void VXCharacter::onStart()
{
    VXActor::onStart();

    m_movement = getComponent<CharacterMovementComponent>();
    if (!m_movement)
    {
        auto *scene = VXGameState::get().getActiveScene();
        if (!scene)
        {
            VX_ENGINE_ERROR_STREAM("VXCharacter::onStart: no active scene — CharacterMovementComponent was not added\n");
            return;
        }
        m_movement = addComponent<CharacterMovementComponent>(scene);
    }
}

void VXCharacter::move(glm::vec3 direction, float deltaTime)
{
    if (m_movement)
        m_movement->move(direction, deltaTime);
}

void VXCharacter::teleport(glm::vec3 worldPos)
{
    if (m_movement)
        m_movement->teleport(worldPos);
    else
        setWorldPosition(worldPos);
}

bool VXCharacter::isGrounded() const
{
    return m_movement ? m_movement->isGrounded() : false;
}

CharacterMovementComponent &VXCharacter::getMovement()
{
    if (!m_movement)
    {
        // Lazy init in case caller invokes getMovement() before onStart()
        auto *scene = VXGameState::get().getActiveScene();
        m_movement = addComponent<CharacterMovementComponent>(scene);
    }
    return *m_movement;
}

ELIX_NESTED_NAMESPACE_END
