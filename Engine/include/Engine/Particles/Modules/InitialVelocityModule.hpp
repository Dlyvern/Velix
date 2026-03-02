#ifndef ELIX_INITIAL_VELOCITY_MODULE_HPP
#define ELIX_INITIAL_VELOCITY_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"

#include <glm/glm.hpp>
#include <random>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

/// Assigns an initial velocity to each newly spawned particle.
/// Final velocity = baseVelocity + random in [-randomness*0.5, +randomness*0.5] per axis.
class InitialVelocityModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::InitialVelocity; }

    void onParticleSpawn(Particle &particle) override;

    glm::vec3 baseVelocity{0.0f, -10.0f, 0.0f};   // m/s
    glm::vec3 randomness{1.0f, 0.5f, 1.0f};        // random offset range per axis

private:
    mutable std::mt19937 m_rng{std::random_device{}()};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_INITIAL_VELOCITY_MODULE_HPP
