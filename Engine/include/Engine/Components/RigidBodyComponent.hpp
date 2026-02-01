#ifndef ELIX_RIGID_BODY_COMPONENT_HPP
#define ELIX_RIGID_BODY_COMPONENT_HPP

#include "Core/Macros.hpp"

#include "Engine/Components/ECS.hpp"

#include "Engine/Physics/PhysXCore.hpp"

#include <glm/vec3.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class RigidBodyComponent : public ECS
{
public:
    explicit RigidBodyComponent(physx::PxRigidActor *rigidActor);
    physx::PxRigidActor *getRigidActor() const;

    void update(float deltaTime) override;

    void setKinematic(bool isKinematic);
    void setGravityEnable(bool enable);
    void setPosition(const physx::PxVec3 &vec);
    void setPosition(const glm::vec3 &vec);

private:
    physx::PxRigidActor *m_rigidActor{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RIGID_BODY_COMPONENT_HPP