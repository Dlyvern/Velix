#ifndef ELIX_FORCE_MODULE_HPP
#define ELIX_FORCE_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"

#include <glm/glm.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

/// Applies constant acceleration (gravity, wind, …) and optional linear drag.
class ForceModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::Force; }

    void onParticleUpdate(Particle &particle, float deltaTime) override;

    glm::vec3 force{0.0f, -9.81f, 0.0f};  // acceleration in m/s²
    float     drag{0.0f};                  // linear drag coefficient (0 = none)
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_FORCE_MODULE_HPP
