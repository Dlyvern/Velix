#ifndef ELIX_COLLISION_MODULE_HPP
#define ELIX_COLLISION_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"
#include "Engine/Physics/PhysicsScene.hpp"

#include <glm/glm.hpp>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

/// Kills particles on physics collision and records hit positions for splash spawning.
/// Usage:
///   1. Add CollisionModule to a rain/snow emitter.
///   2. Set splashEmitterName to the name of the splash emitter in the same ParticleSystem.
///   3. The ParticleSystem::update() loop automatically feeds hit positions to the splash emitter.
class CollisionModule : public IParticleModule
{
public:
    std::string splashEmitterName;

    float lookAheadMultiplier{1.5f};

    uint32_t splashCount{6};

    ParticleModuleType getType() const override { return ParticleModuleType::Collision; }

    void setPhysicsScene(PhysicsScene *scene) override { m_physicsScene = scene; }

    void onParticleUpdate(Particle &p, float dt) override;

    const std::vector<glm::vec3> &getHitPositions() const { return m_hitPositions; }
    void clearHits() { m_hitPositions.clear(); }

private:
    PhysicsScene *m_physicsScene{nullptr};
    std::vector<glm::vec3> m_hitPositions;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_COLLISION_MODULE_HPP
