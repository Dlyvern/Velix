#ifndef ELIX_PARTICLE_EMITTER_HPP
#define ELIX_PARTICLE_EMITTER_HPP

#include "Engine/Particles/ParticleTypes.hpp"
#include "Engine/Particles/IParticleModule.hpp"

#include "Engine/Physics/PhysicsScene.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

/// A single particle emitter: owns a fixed-size particle pool and a module stack.
/// Multiple emitters live inside one ParticleSystem.
class ParticleEmitter
{
public:
    static constexpr uint32_t MAX_PARTICLES = 10000;

    std::string name{"Emitter"};
    bool enabled{true};

    explicit ParticleEmitter(const std::string &name = "Emitter");
    ~ParticleEmitter() = default;

    template <typename T, typename... Args>
    T *addModule(Args &&...args)
    {
        static_assert(std::is_base_of_v<IParticleModule, T>,
                      "ParticleEmitter::addModule() T must derive from IParticleModule");
        auto mod = std::make_shared<T>(std::forward<Args>(args)...);
        T *ptr = mod.get();
        m_modules[std::type_index(typeid(T))] = std::move(mod);
        return ptr;
    }

    template <typename T>
    T *getModule() const
    {
        auto it = m_modules.find(std::type_index(typeid(T)));
        return it != m_modules.end() ? static_cast<T *>(it->second.get()) : nullptr;
    }

    template <typename T>
    bool hasModule() const
    {
        return m_modules.count(std::type_index(typeid(T))) > 0;
    }

    template <typename T>
    void removeModule()
    {
        m_modules.erase(std::type_index(typeid(T)));
    }

    void update(float deltaTime, const glm::vec3 &worldPosition);

    /// Propagates the physics scene pointer to all modules that support collision.
    void setPhysicsScene(PhysicsScene *scene);

    /// Spawns a burst of particles at an explicit world position, bypassing the emitter's
    /// shape module. Used for splash effects driven by CollisionModule hit positions.
    void spawnParticleAt(const glm::vec3 &worldPos, uint32_t count = 1);

    void play();
    void stop();
    void reset();

    bool isPlaying() const { return m_playing; }

    const std::vector<Particle> &getParticles() const { return m_particles; }
    uint32_t getAliveCount() const { return m_aliveCount; }

    bool isDirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }

    /// Returns positions where particles died this frame (cleared at the start of each update).
    const std::vector<glm::vec3> &getDeathPositions() const { return m_deathPositions; }

private:
    std::unordered_map<std::type_index, std::shared_ptr<IParticleModule>> m_modules;

    std::vector<Particle> m_particles;       // pre-allocated pool
    std::vector<uint32_t> m_freeList;        // indices of dead particles
    std::vector<glm::vec3> m_deathPositions; // positions of particles that died this frame
    uint32_t m_aliveCount{0};

    float m_spawnAccumulator{0.0f};
    float m_time{0.0f};
    bool m_playing{false};
    bool m_burstFired{false};
    bool m_dirty{false};

    uint32_t allocateParticle();
    void freeParticle(uint32_t index);
    void spawnParticle(const glm::vec3 &emitterPos);
    void applySpawnModules(Particle &p, const glm::vec3 &emitterPos);
    void applyUpdateModules(Particle &p, float dt);
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PARTICLE_EMITTER_HPP
