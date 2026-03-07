#ifndef ELIX_PHYSX_CORE_HPP
#define ELIX_PHYSX_CORE_HPP

#define PX_PHYSX_STATIC_LIB
#include "PxPhysicsAPI.h"
#if defined(PHYSX_GPU_ENABLED) && PX_SUPPORT_GPU_PHYSX
#include "gpu/PxGpu.h"
#endif
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

    static bool init();

    static void shutdown();

    static PhysXCore *getInstance();

    physx::PxPhysics *getPhysics();

    bool isGPUEnabled() const;

#if defined(PHYSX_GPU_ENABLED) && PX_SUPPORT_GPU_PHYSX
    physx::PxCudaContextManager *getCudaContextManager();
#endif

private:
    PhysXCore();

    static inline PhysXCore *m_instance{nullptr};

    PhysXMessenger m_messenger;

    physx::PxPhysics *m_physics{nullptr};
    physx::PxFoundation *m_foundation{nullptr};
    physx::PxDefaultAllocator m_defaultAllocator;
    physx::PxPvd *m_pvd{nullptr};

#if defined(PHYSX_GPU_ENABLED) && PX_SUPPORT_GPU_PHYSX
    physx::PxCudaContextManager *m_cudaContextManager{nullptr};
#endif
    bool m_gpuEnabled{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PHYSX_CORE_HPP