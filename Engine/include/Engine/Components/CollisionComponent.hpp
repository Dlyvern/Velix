#ifndef ELIX_COLLISION_COMPONENT_HPP
#define ELIX_COLLISION_COMPONENT_HPP

#include "Engine/Components/ECS.hpp"

#include "Engine/Physics/PhysXCore.hpp"

#include <glm/vec3.hpp>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class CollisionComponent : public ECS
{
public:
    enum class ShapeType : uint8_t
    {
        BOX = 0,
        CAPSULE = 1
    };

    explicit CollisionComponent(
        physx::PxShape *shape,
        ShapeType shapeType = ShapeType::BOX,
        const glm::vec3 &boxHalfExtents = glm::vec3(0.5f),
        float capsuleRadius = 0.5f,
        float capsuleHalfHeight = 0.5f,
        physx::PxRigidStatic *actor = nullptr);

    void update(float deltaTime) override;

    physx::PxShape *getShape();
    physx::PxRigidStatic *getActor();
    ShapeType getShapeType() const;

    const glm::vec3 &getBoxHalfExtents() const;
    float getCapsuleRadius() const;
    float getCapsuleHalfHeight() const;

    bool setBoxHalfExtents(const glm::vec3 &halfExtents);
    bool setCapsuleDimensions(float radius, float halfHeight);

    void removeActor();

private:
    void cacheGeometryFromShape();

    physx::PxShape *m_shape{nullptr};
    ShapeType m_shapeType{ShapeType::BOX};
    glm::vec3 m_boxHalfExtents{0.5f};
    float m_capsuleRadius{0.5f};
    float m_capsuleHalfHeight{0.5f};
    physx::PxRigidStatic *m_actor{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_COLLISION_COMPONENT_HPP
