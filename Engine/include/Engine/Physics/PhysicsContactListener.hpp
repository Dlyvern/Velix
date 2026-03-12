#ifndef ELIX_PHYSICS_CONTACT_LISTENER_HPP
#define ELIX_PHYSICS_CONTACT_LISTENER_HPP

#include "Core/Macros.hpp"
#include "Engine/Physics/PhysXCore.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class PhysicsContactListener : public physx::PxSimulationEventCallback
{
public:
    void onContact(const physx::PxContactPairHeader &pairHeader,
                   const physx::PxContactPair *pairs,
                   physx::PxU32 nbPairs) override;

    void onTrigger(physx::PxTriggerPair *pairs, physx::PxU32 count) override;

    void onConstraintBreak(physx::PxConstraintInfo *, physx::PxU32) override {}
    void onWake(physx::PxActor **, physx::PxU32) override {}
    void onSleep(physx::PxActor **, physx::PxU32) override {}
    void onAdvance(const physx::PxRigidBody *const *, const physx::PxTransform *, physx::PxU32) override {}
};

physx::PxFilterFlags contactNotifyFilterShader(
    physx::PxFilterObjectAttributes attributes0, physx::PxFilterData filterData0,
    physx::PxFilterObjectAttributes attributes1, physx::PxFilterData filterData1,
    physx::PxPairFlags &pairFlags,
    const void *constantBlock, physx::PxU32 constantBlockSize);

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PHYSICS_CONTACT_LISTENER_HPP
