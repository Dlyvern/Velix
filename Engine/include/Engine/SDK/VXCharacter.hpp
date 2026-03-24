#ifndef ELIX_VX_CHARACTER_HPP
#define ELIX_VX_CHARACTER_HPP

#include "Engine/SDK/VXActor.hpp"
#include "Engine/Components/CharacterMovementComponent.hpp"

#include <glm/glm.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

// VXCharacter extends VXActor with automatic character movement setup.
// On onStart(), a CharacterMovementComponent is added if not already present.
// Subclass VXCharacter for any entity that needs physics-based movement (player, AI, etc.).
class VXCharacter : public VXActor
{
public:
    void onStart() override;

    // Move the character by the given world-space displacement this frame.
    // `direction` should already be scaled by speed and deltaTime:
    //   move(getForward() * moveSpeed * dt);
    void move(glm::vec3 direction, float deltaTime);

    // Teleport the character to an absolute world position (bypasses physics).
    void teleport(glm::vec3 worldPos);

    // Returns true when the character is in contact with the ground.
    bool isGrounded() const;

    // Access the underlying movement component directly.
    CharacterMovementComponent &getMovement();

protected:
    CharacterMovementComponent *m_movement{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_VX_CHARACTER_HPP
