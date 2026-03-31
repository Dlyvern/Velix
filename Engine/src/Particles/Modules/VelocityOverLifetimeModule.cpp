#include "Engine/Particles/Modules/VelocityOverLifetimeModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void VelocityOverLifetimeModule::onParticleUpdate(Particle &particle, float /*deltaTime*/)
{
    if (!m_enabled)
        return;

    const float t = particle.getNormalizedAge();
    const float multiplier = evaluateCurve(speedCurve, t);
    particle.velocity *= multiplier;
}

ELIX_NESTED_NAMESPACE_END
