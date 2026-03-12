#include "Engine/Physics/PhysicsContactListener.hpp"

#include "Engine/Components/ScriptComponent.hpp"
#include "Engine/Entity.hpp"
#include "Engine/Scripting/Script.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

// Helper: dispatch a callback to every Script on an entity
namespace
{
    Entity *entityFromActor(const physx::PxActor *actor)
    {
        return actor ? static_cast<Entity *>(actor->userData) : nullptr;
    }

    void dispatchCollisionEnter(Entity *entity, Entity *other, const CollisionInfo &info)
    {
        if (!entity)
            return;
        for (auto *sc : entity->getComponents<ScriptComponent>())
        {
            if (sc && sc->getScript())
                sc->getScript()->onCollisionEnter(other, info);
        }
    }

    void dispatchCollisionStay(Entity *entity, Entity *other, const CollisionInfo &info)
    {
        if (!entity)
            return;
        for (auto *sc : entity->getComponents<ScriptComponent>())
        {
            if (sc && sc->getScript())
                sc->getScript()->onCollisionStay(other, info);
        }
    }

    void dispatchCollisionExit(Entity *entity, Entity *other)
    {
        if (!entity)
            return;
        for (auto *sc : entity->getComponents<ScriptComponent>())
        {
            if (sc && sc->getScript())
                sc->getScript()->onCollisionExit(other);
        }
    }

    void dispatchTriggerEnter(Entity *entity, Entity *other)
    {
        if (!entity)
            return;
        for (auto *sc : entity->getComponents<ScriptComponent>())
        {
            if (sc && sc->getScript())
                sc->getScript()->onTriggerEnter(other);
        }
    }

    void dispatchTriggerExit(Entity *entity, Entity *other)
    {
        if (!entity)
            return;
        for (auto *sc : entity->getComponents<ScriptComponent>())
        {
            if (sc && sc->getScript())
                sc->getScript()->onTriggerExit(other);
        }
    }
} // namespace

void PhysicsContactListener::onContact(const physx::PxContactPairHeader &pairHeader,
                                       const physx::PxContactPair *pairs,
                                       physx::PxU32 nbPairs)
{
    // Actors may be deleted — PhysX marks them with eREMOVED_ACTOR flags
    if (pairHeader.flags & (physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_0 |
                            physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
        return;

    Entity *entityA = entityFromActor(pairHeader.actors[0]);
    Entity *entityB = entityFromActor(pairHeader.actors[1]);

    for (physx::PxU32 i = 0; i < nbPairs; ++i)
    {
        const physx::PxContactPair &cp = pairs[i];

        // Build a CollisionInfo from the first contact point (if available)
        CollisionInfo info;
        if (cp.contactCount > 0)
        {
            physx::PxContactPairPoint contactPoints[1];
            cp.extractContacts(contactPoints, 1);
            info.contactPoint  = glm::vec3(contactPoints[0].position.x,
                                           contactPoints[0].position.y,
                                           contactPoints[0].position.z);
            info.contactNormal = glm::vec3(contactPoints[0].normal.x,
                                           contactPoints[0].normal.y,
                                           contactPoints[0].normal.z);
            info.impulse = contactPoints[0].impulse.magnitude();
        }

        if (cp.events & physx::PxPairFlag::eNOTIFY_TOUCH_FOUND)
        {
            dispatchCollisionEnter(entityA, entityB, info);
            dispatchCollisionEnter(entityB, entityA, info);
        }
        else if (cp.events & physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS)
        {
            dispatchCollisionStay(entityA, entityB, info);
            dispatchCollisionStay(entityB, entityA, info);
        }
        else if (cp.events & physx::PxPairFlag::eNOTIFY_TOUCH_LOST)
        {
            dispatchCollisionExit(entityA, entityB);
            dispatchCollisionExit(entityB, entityA);
        }
    }
}

void PhysicsContactListener::onTrigger(physx::PxTriggerPair *pairs, physx::PxU32 count)
{
    for (physx::PxU32 i = 0; i < count; ++i)
    {
        const physx::PxTriggerPair &tp = pairs[i];

        // Skip pairs involving deleted actors/shapes
        if (tp.flags & (physx::PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER |
                        physx::PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
            continue;

        Entity *trigger = entityFromActor(tp.triggerActor);
        Entity *other   = entityFromActor(tp.otherActor);

        if (tp.status & physx::PxPairFlag::eNOTIFY_TOUCH_FOUND)
        {
            dispatchTriggerEnter(trigger, other);
            dispatchTriggerEnter(other, trigger);
        }
        else if (tp.status & physx::PxPairFlag::eNOTIFY_TOUCH_LOST)
        {
            dispatchTriggerExit(trigger, other);
            dispatchTriggerExit(other, trigger);
        }
    }
}

physx::PxFilterFlags contactNotifyFilterShader(
    physx::PxFilterObjectAttributes attributes0, physx::PxFilterData /*filterData0*/,
    physx::PxFilterObjectAttributes attributes1, physx::PxFilterData /*filterData1*/,
    physx::PxPairFlags &pairFlags,
    const void * /*constantBlock*/, physx::PxU32 /*constantBlockSize*/)
{
    // Let triggers through
    if (physx::PxFilterObjectIsTrigger(attributes0) || physx::PxFilterObjectIsTrigger(attributes1))
    {
        pairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT;
        return physx::PxFilterFlags();
    }

    // Standard collision + all contact notification events + contact points
    pairFlags = physx::PxPairFlag::eCONTACT_DEFAULT
              | physx::PxPairFlag::eNOTIFY_TOUCH_FOUND
              | physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS
              | physx::PxPairFlag::eNOTIFY_TOUCH_LOST
              | physx::PxPairFlag::eNOTIFY_CONTACT_POINTS;

    return physx::PxFilterFlags();
}

ELIX_NESTED_NAMESPACE_END
