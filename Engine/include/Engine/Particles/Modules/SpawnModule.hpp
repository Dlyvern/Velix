#ifndef ELIX_SPAWN_MODULE_HPP
#define ELIX_SPAWN_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"

#include <glm/glm.hpp>
#include <random>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

enum class EmitterShape : uint8_t
{
    Point = 0, // all particles at emitter origin
    Sphere,    // inside or on a sphere
    Box,       // inside a box (half-extents)
    Cone,      // within a cone (downward by default)
    Cylinder,  // inside a cylinder
};

struct EmitterShapeConfig
{
    EmitterShape shape{EmitterShape::Point};

    float radius{1.0f};      // Sphere / Cone / Cylinder
    glm::vec3 extents{1.0f}; // Box half-extents
    float angle{25.0f};      // Cone half-angle (degrees)
    float height{1.0f};      // Cone / Cylinder height
    bool surfaceOnly{false}; // spawn on surface instead of volume
};

/// Controls how many particles are created, how often, and from what shape.
class SpawnModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::Spawn; }

    float spawnRate{100.0f}; // continuous: particles per second
    float burstCount{0.0f};  // one-shot burst at t == 0 (0 = none)

    bool loop{true};
    float duration{5.0f}; // total system lifetime when loop == false

    EmitterShapeConfig shape;

    // Sub-emitter: when set, triggers a burst in another emitter when particles die
    std::string subEmitterOnDeath;
    int subEmitterBurstCount{1};

    glm::vec3 samplePosition(const glm::vec3 &emitterWorldPos) const;

    glm::vec3 sampleDirection() const;

    void onParticleSpawn(Particle &particle) override;

    void setEmitterWorldPosition(const glm::vec3 &pos) { m_emitterWorldPos = pos; }

private:
    mutable std::mt19937 m_rng{std::random_device{}()};
    glm::vec3 m_emitterWorldPos{0.0f};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SPAWN_MODULE_HPP
