#ifndef ELIX_VX_CHARACTER_HPP
#define ELIX_VX_CHARACTER_HPP

#include "Engine/SDK/VXActor.hpp"
#include "Engine/Components/CharacterMovementComponent.hpp"

#include <glm/glm.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)




class VXCharacter : public VXActor
{
public:
    void onStart() override;




    void move(glm::vec3 direction, float deltaTime);


    void teleport(glm::vec3 worldPos);


    bool isGrounded() const;


    CharacterMovementComponent &getMovement();

protected:
    CharacterMovementComponent *m_movement{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif
