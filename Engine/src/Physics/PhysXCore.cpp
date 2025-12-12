#include "Engine/Physics/PhysXCore.hpp"
#include <iostream>


struct UserErrorCallback final : physx::PxErrorCallback
{
    void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, const int line) override
    {
        std::cout << file << " line " << line << ": " << message << "\n" << "\n";
    }
} gPhysXErrorCallback;

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void PhysXCore::init()
{
    if(!m_instance)
        m_instance = new PhysXCore();

    m_instance->m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_instance->m_defaultAllocator, gPhysXErrorCallback);
    m_instance->m_pvd = PxCreatePvd(*m_instance->m_foundation);

    physx::PxTolerancesScale scale;
    m_instance->m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_instance->m_foundation, scale, true, m_instance->m_pvd);

    physx::PxSceneDesc sceneDesc(m_instance->m_physics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);
    sceneDesc.cpuDispatcher = physx::PxDefaultCpuDispatcherCreate(2);
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
    m_instance->m_scene = m_instance->m_physics->createScene(sceneDesc);

    m_instance->m_defaultMaterial = m_instance->m_physics->createMaterial(0.5f, 0.5f, 0.6f);

    m_instance->m_controllerManager = PxCreateControllerManager(*m_instance->m_scene);
}

void PhysXCore::shutdown()
{
    if (m_instance->m_scene)
    {
        m_instance->m_scene->release();
        m_instance->m_scene = nullptr;
    }

    if (m_instance->m_physics)
    {
        m_instance->m_physics->release();
        m_instance->m_physics = nullptr;
    }

    if (m_instance->m_foundation)
    {
        m_instance->m_foundation->release();
        m_instance->m_foundation = nullptr;
    }
    
    delete m_instance;
    m_instance = nullptr;
}


ELIX_NESTED_NAMESPACE_END