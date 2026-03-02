#ifndef ELIX_SIZE_OVER_LIFETIME_MODULE_HPP
#define ELIX_SIZE_OVER_LIFETIME_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

#include <glm/glm.hpp>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

/// Scales particle size against a normalised-age curve each frame.
/// particle.size = baseSize * curve(normalizedAge)
class SizeOverLifetimeModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::SizeOverLifetime; }

    void onParticleUpdate(Particle &particle, float deltaTime) override;

    glm::vec2 baseSize{0.1f, 0.1f};    // world-space starting size (width, height)

    // Default: constant size throughout life
    std::vector<CurvePoint> curve{
        {0.0f, 1.0f},
        {1.0f, 1.0f}
    };
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SIZE_OVER_LIFETIME_MODULE_HPP
