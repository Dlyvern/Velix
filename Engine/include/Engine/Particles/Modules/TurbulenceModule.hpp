#ifndef ELIX_TURBULENCE_MODULE_HPP
#define ELIX_TURBULENCE_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct Particle;

/// Adds hash-based value noise to particle velocity every frame, creating organic drift.
class TurbulenceModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::Turbulence; }

    void onParticleUpdate(Particle &particle, float deltaTime) override;

    float strength{1.0f};     // velocity perturbation magnitude (m/s per second)
    float frequency{1.0f};    // noise spatial frequency (higher = tighter swirls)
    float scrollSpeed{0.5f};  // how fast the noise pattern moves over time
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_TURBULENCE_MODULE_HPP
