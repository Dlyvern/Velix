#ifndef ELIX_COLOR_OVER_LIFETIME_MODULE_HPP
#define ELIX_COLOR_OVER_LIFETIME_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)


class ColorOverLifetimeModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::ColorOverLifetime; }

    void onParticleUpdate(Particle &particle, float deltaTime) override;


    std::vector<GradientPoint> gradient{
        {0.0f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)},
        {1.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)}
    };
};

ELIX_NESTED_NAMESPACE_END

#endif
