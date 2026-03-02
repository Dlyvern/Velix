#ifndef ELIX_LIFETIME_MODULE_HPP
#define ELIX_LIFETIME_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"

#include <random>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

/// Sets a random lifetime for each spawned particle in [minLifetime, maxLifetime].
class LifetimeModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::Lifetime; }

    void onParticleSpawn(Particle &particle) override;

    float minLifetime{1.0f};
    float maxLifetime{2.0f};

private:
    mutable std::mt19937 m_rng{std::random_device{}()};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_LIFETIME_MODULE_HPP
