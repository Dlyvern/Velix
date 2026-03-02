#include "Engine/Particles/Modules/LifetimeModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

#include <algorithm>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void LifetimeModule::onParticleSpawn(Particle &particle)
{
    const float lo = std::min(minLifetime, maxLifetime);
    const float hi = std::max(minLifetime, maxLifetime);
    std::uniform_real_distribution<float> dist(lo, hi);
    particle.lifetime = dist(m_rng);
}

ELIX_NESTED_NAMESPACE_END
