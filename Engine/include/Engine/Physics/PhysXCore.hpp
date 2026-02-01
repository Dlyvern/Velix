#ifndef ELIX_PHYSX_CORE_HPP
#define ELIX_PHYSX_CORE_HPP

#define PX_PHYSX_STATIC_LIB
#include "PxPhysicsAPI.h"
#include "Core/Macros.hpp"
#include "Engine/Physics/PhysXMessenger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class PhysXCore
{
public:
    PhysXCore(const PhysXCore &) = delete;
    PhysXCore &operator=(const PhysXCore &) = delete;

    PhysXCore(PhysXCore &&) noexcept = default;
    PhysXCore &operator=(PhysXCore &&) noexcept = default;

    static void init();

    static void shutdown();

    static PhysXCore *getInstance();

    physx::PxPhysics *getPhysics();

private:
    PhysXCore();

    static inline PhysXCore *m_instance{nullptr};

    PhysXMessenger m_messenger;

    physx::PxPhysics *m_physics{nullptr};
    physx::PxFoundation *m_foundation{nullptr};
    physx::PxDefaultAllocator m_defaultAllocator;
    physx::PxPvd *m_pvd{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PHYSX_CORE_HPP