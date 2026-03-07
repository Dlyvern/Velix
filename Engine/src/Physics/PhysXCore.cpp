#include "Engine/Physics/PhysXCore.hpp"
#include "Core/Logger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

PhysXCore::PhysXCore() : m_messenger(core::Logger::getDefaultLogger())
{
}

PhysXCore *PhysXCore::getInstance()
{
    return m_instance;
}

physx::PxPhysics *PhysXCore::getPhysics()
{
    return m_physics;
}

bool PhysXCore::isGPUEnabled() const
{
    return m_gpuEnabled;
}

#if defined(PHYSX_GPU_ENABLED) && PX_SUPPORT_GPU_PHYSX
physx::PxCudaContextManager *PhysXCore::getCudaContextManager()
{
    return m_cudaContextManager;
}
#endif

bool PhysXCore::init()
{
    if (!m_instance)
        m_instance = new PhysXCore();

    m_instance->m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_instance->m_defaultAllocator, m_instance->m_messenger);

    if (!m_instance->m_foundation)
    {
        VX_ENGINE_ERROR_STREAM("Failed to create foundation");
        return false;
    }

    m_instance->m_pvd = PxCreatePvd(*m_instance->m_foundation);

    if (!m_instance->m_pvd)
    {
        VX_ENGINE_ERROR_STREAM("Failed to create pvd");
        return false;
    }

    physx::PxTolerancesScale scale;
    m_instance->m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_instance->m_foundation, scale, true, m_instance->m_pvd);

    if (!m_instance->m_physics)
    {
        VX_ENGINE_ERROR_STREAM("physics");
        return false;
    }

#if defined(PHYSX_GPU_ENABLED) && PX_SUPPORT_GPU_PHYSX
    {
        const int cudaOrdinal = PxGetSuggestedCudaDeviceOrdinal(m_instance->m_messenger);
        if (cudaOrdinal >= 0)
        {
            physx::PxCudaContextManagerDesc cudaDesc;
            cudaDesc.interopMode = physx::PxCudaInteropMode::NO_INTEROP;
            m_instance->m_cudaContextManager = PxCreateCudaContextManager(
                *m_instance->m_foundation, cudaDesc);

            if (m_instance->m_cudaContextManager && m_instance->m_cudaContextManager->contextIsValid())
            {
                m_instance->m_gpuEnabled = true;
                VX_ENGINE_INFO_STREAM("PhysX GPU acceleration enabled (CUDA device " << cudaOrdinal << ")");
            }
            else
            {
                if (m_instance->m_cudaContextManager)
                {
                    m_instance->m_cudaContextManager->release();
                    m_instance->m_cudaContextManager = nullptr;
                }
                VX_ENGINE_WARNING_STREAM("PhysX CUDA context invalid — falling back to CPU");
            }
        }
        else
        {
            VX_ENGINE_INFO_STREAM("No CUDA-capable GPU found — PhysX running on CPU");
        }
    }
#endif

    return true;
}

void PhysXCore::shutdown()
{
    if (m_instance->m_physics)
    {
        m_instance->m_physics->release();
        m_instance->m_physics = nullptr;
    }

#if defined(PHYSX_GPU_ENABLED) && PX_SUPPORT_GPU_PHYSX
    if (m_instance->m_cudaContextManager)
    {
        m_instance->m_cudaContextManager->release();
        m_instance->m_cudaContextManager = nullptr;
    }
#endif

    if (m_instance->m_foundation)
    {
        m_instance->m_foundation->release();
        m_instance->m_foundation = nullptr;
    }

    delete m_instance;
    m_instance = nullptr;
}

ELIX_NESTED_NAMESPACE_END