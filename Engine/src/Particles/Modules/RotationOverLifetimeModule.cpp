#include "Engine/Particles/Modules/RotationOverLifetimeModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

#include <algorithm>
#include <cstdlib>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void RotationOverLifetimeModule::onParticleSpawn(Particle &particle)
{
    if (!m_enabled)
        return;

    const float range = angularVelocityMax - angularVelocityMin;
    const float t = (range > 0.0f) ? (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) : 0.0f;
    particle.rotationSpeed = angularVelocityMin + t * range;
}

void RotationOverLifetimeModule::onParticleUpdate(Particle &particle, float deltaTime)
{
    if (!m_enabled)
        return;

    particle.rotation += particle.rotationSpeed * deltaTime;
}

ELIX_NESTED_NAMESPACE_END
