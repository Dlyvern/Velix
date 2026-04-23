#ifndef ELIX_SIZE_OVER_LIFETIME_MODULE_HPP
#define ELIX_SIZE_OVER_LIFETIME_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

#include <glm/glm.hpp>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)



class SizeOverLifetimeModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::SizeOverLifetime; }

    void onParticleUpdate(Particle &particle, float deltaTime) override;

    glm::vec2 baseSize{0.1f, 0.1f};


    std::vector<CurvePoint> curve{
        {0.0f, 1.0f},
        {1.0f, 1.0f}
    };
};

ELIX_NESTED_NAMESPACE_END

#endif
