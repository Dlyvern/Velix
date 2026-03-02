#include "Engine/Particles/Modules/SizeOverLifetimeModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void SizeOverLifetimeModule::onParticleUpdate(Particle &particle, float /*deltaTime*/)
{
    const float scale = evaluateCurve(curve, particle.getNormalizedAge());
    particle.size = baseSize * scale;
}

ELIX_NESTED_NAMESPACE_END
