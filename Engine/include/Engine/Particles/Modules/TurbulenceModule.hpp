#ifndef ELIX_TURBULENCE_MODULE_HPP
#define ELIX_TURBULENCE_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct Particle;


class TurbulenceModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::Turbulence; }

    void onParticleUpdate(Particle &particle, float deltaTime) override;

    float strength{1.0f};
    float frequency{1.0f};
    float scrollSpeed{0.5f};
};

ELIX_NESTED_NAMESPACE_END

#endif
