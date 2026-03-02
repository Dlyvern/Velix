#include "Engine/Particles/Modules/ColorOverLifetimeModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void ColorOverLifetimeModule::onParticleUpdate(Particle &particle, float /*deltaTime*/)
{
    particle.color = evaluateGradient(gradient, particle.getNormalizedAge());
}

ELIX_NESTED_NAMESPACE_END
