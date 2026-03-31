#ifndef ELIX_PARTICLE_SYSTEM_HPP
#define ELIX_PARTICLE_SYSTEM_HPP

#include "Engine/Particles/ParticleEmitter.hpp"

#include "Engine/Physics/PhysicsScene.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

/// A particle system groups one or more ParticleEmitters that share a world position.
/// Analogous to a Niagara System asset — can be saved / shared / assigned to a component.
class ParticleSystem
{
public:
    using SharedPtr = std::shared_ptr<ParticleSystem>;

    std::string name{"Particle System"};

    ParticleEmitter *addEmitter(const std::string &emitterName = "Emitter");
    ParticleEmitter *getEmitter(const std::string &emitterName) const;
    void removeEmitter(const std::string &emitterName);

    const std::vector<std::unique_ptr<ParticleEmitter>> &getEmitters() const { return m_emitters; }

    /// Called every frame by ParticleSystemComponent.
    void update(float deltaTime, const glm::vec3 &worldPosition);

    /// Injects the physics scene into all emitters so CollisionModules can raycast.
    void setPhysicsScene(PhysicsScene *scene);

    void play();
    void stop();
    void pause();
    void reset();

    bool isPlaying() const { return m_playing && !m_paused; }
    bool isPaused() const { return m_paused; }
    bool isStopped() const { return !m_playing; }

private:
    std::vector<std::unique_ptr<ParticleEmitter>> m_emitters;

    bool m_playing{false};
    bool m_paused{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PARTICLE_SYSTEM_HPP
