#include "Engine/Physics/PhysXCore.hpp"
#include <iostream>

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

void PhysXCore::init()
{
    if (!m_instance)
        m_instance = new PhysXCore();

    m_instance->m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_instance->m_defaultAllocator, m_instance->m_messenger);
    m_instance->m_pvd = PxCreatePvd(*m_instance->m_foundation);

    physx::PxTolerancesScale scale;
    m_instance->m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_instance->m_foundation, scale, true, m_instance->m_pvd);
}

void PhysXCore::shutdown()
{
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