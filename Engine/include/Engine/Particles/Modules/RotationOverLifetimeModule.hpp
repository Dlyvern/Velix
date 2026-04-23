#ifndef ELIX_ROTATION_OVER_LIFETIME_MODULE_HPP
#define ELIX_ROTATION_OVER_LIFETIME_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct Particle;


class RotationOverLifetimeModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::RotationOverLifetime; }

    void onParticleSpawn(Particle &particle) override;
    void onParticleUpdate(Particle &particle, float deltaTime) override;

    float angularVelocityMin{-1.0f};
    float angularVelocityMax{ 1.0f};
};

ELIX_NESTED_NAMESPACE_END

#endif
