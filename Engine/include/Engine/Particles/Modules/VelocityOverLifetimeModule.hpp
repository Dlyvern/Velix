#ifndef ELIX_VELOCITY_OVER_LIFETIME_MODULE_HPP
#define ELIX_VELOCITY_OVER_LIFETIME_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

/// Multiplies the particle's velocity magnitude by a curve over normalised age.
class VelocityOverLifetimeModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::VelocityOverLifetime; }

    void onParticleUpdate(Particle &particle, float deltaTime) override;

    // Curve value is a speed multiplier. Default: constant 1 (no change).
    std::vector<CurvePoint> speedCurve{
        {0.0f, 1.0f},
        {1.0f, 1.0f}
    };
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_VELOCITY_OVER_LIFETIME_MODULE_HPP
