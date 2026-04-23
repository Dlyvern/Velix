#ifndef ELIX_INITIAL_VELOCITY_MODULE_HPP
#define ELIX_INITIAL_VELOCITY_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"

#include <glm/glm.hpp>
#include <random>

ELIX_NESTED_NAMESPACE_BEGIN(engine)



class InitialVelocityModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::InitialVelocity; }

    void onParticleSpawn(Particle &particle) override;

    glm::vec3 baseVelocity{0.0f, -10.0f, 0.0f};
    glm::vec3 randomness{1.0f, 0.5f, 1.0f};

private:
    mutable std::mt19937 m_rng{std::random_device{}()};
};

ELIX_NESTED_NAMESPACE_END

#endif
