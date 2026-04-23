#ifndef ELIX_SPAWN_MODULE_HPP
#define ELIX_SPAWN_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"

#include <glm/glm.hpp>
#include <random>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

enum class EmitterShape : uint8_t
{
    Point = 0,
    Sphere,
    Box,
    Cone,
    Cylinder,
};

struct EmitterShapeConfig
{
    EmitterShape shape{EmitterShape::Point};

    float radius{1.0f};
    glm::vec3 extents{1.0f};
    float angle{25.0f};
    float height{1.0f};
    bool surfaceOnly{false};
};


class SpawnModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::Spawn; }

    float spawnRate{100.0f};
    float burstCount{0.0f};

    bool loop{true};
    float duration{5.0f};

    EmitterShapeConfig shape;


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

#endif
