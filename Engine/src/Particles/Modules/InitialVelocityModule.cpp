#include "Engine/Particles/Modules/InitialVelocityModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void InitialVelocityModule::onParticleSpawn(Particle &particle)
{
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

    particle.velocity = baseVelocity + glm::vec3(
        randomness.x * dist(m_rng),
        randomness.y * dist(m_rng),
        randomness.z * dist(m_rng));
}

ELIX_NESTED_NAMESPACE_END
