#include "Engine/Particles/Modules/ForceModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void ForceModule::onParticleUpdate(Particle &particle, float deltaTime)
{
    // Integrate acceleration
    particle.velocity += force * deltaTime;

    // Linear drag: v' = v * (1 - drag * dt),  clamped to never reverse direction
    if (drag > 0.0f)
        particle.velocity *= std::max(0.0f, 1.0f - drag * deltaTime);
}

ELIX_NESTED_NAMESPACE_END
