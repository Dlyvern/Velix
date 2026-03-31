#ifndef ELIX_ROTATION_OVER_LIFETIME_MODULE_HPP
#define ELIX_ROTATION_OVER_LIFETIME_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct Particle;

/// Sets a random angular velocity at spawn and integrates it each frame.
class RotationOverLifetimeModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::RotationOverLifetime; }

    void onParticleSpawn(Particle &particle) override;
    void onParticleUpdate(Particle &particle, float deltaTime) override;

    float angularVelocityMin{-1.0f}; // rad/s
    float angularVelocityMax{ 1.0f}; // rad/s
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ROTATION_OVER_LIFETIME_MODULE_HPP
