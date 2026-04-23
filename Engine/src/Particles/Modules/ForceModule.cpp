#include "Engine/Particles/Modules/ForceModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void ForceModule::onParticleUpdate(Particle &particle, float deltaTime)
{

    particle.velocity += force * deltaTime;


    if (drag > 0.0f)
        particle.velocity *= std::max(0.0f, 1.0f - drag * deltaTime);
}

ELIX_NESTED_NAMESPACE_END
