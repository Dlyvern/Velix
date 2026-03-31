#include "Engine/Particles/ParticleEmitter.hpp"
#include "Engine/Particles/Modules/SpawnModule.hpp"

#include <algorithm>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ParticleEmitter::ParticleEmitter(const std::string &emitterName)
    : name(emitterName)
{
    m_particles.resize(MAX_PARTICLES);
    m_freeList.reserve(MAX_PARTICLES);
    for (uint32_t i = 0; i < MAX_PARTICLES; ++i)
        m_freeList.push_back(MAX_PARTICLES - 1 - i); // fill in reverse so index 0 is first out
}

void ParticleEmitter::update(float deltaTime, const glm::vec3 &worldPosition)
{
    if (!enabled || !m_playing)
        return;

    m_time += deltaTime;
    m_deathPositions.clear();

    auto *spawn = getModule<SpawnModule>();

    if (spawn)
        spawn->setEmitterWorldPosition(worldPosition);

    if (!m_burstFired && spawn && spawn->burstCount > 0.0f)
    {
        const auto count = static_cast<uint32_t>(spawn->burstCount);
        for (uint32_t i = 0; i < count; ++i)
            spawnParticle(worldPosition);
        m_burstFired = true;
    }

    if (spawn && spawn->spawnRate > 0.0f)
    {
        m_spawnAccumulator += spawn->spawnRate * deltaTime;
        const auto count = static_cast<uint32_t>(m_spawnAccumulator);
        m_spawnAccumulator -= static_cast<float>(count);
        for (uint32_t i = 0; i < count; ++i)
            spawnParticle(worldPosition);
    }

    uint32_t newAliveCount = 0;
    for (uint32_t i = 0; i < MAX_PARTICLES; ++i)
    {
        Particle &p = m_particles[i];
        if (!p.alive)
            continue;

        p.age += deltaTime;

        if (p.isDead())
        {
            m_deathPositions.push_back(p.position);
            freeParticle(i);
        }
        else
        {
            p.position += p.velocity * deltaTime;
            p.rotation += p.rotationSpeed * deltaTime;
            applyUpdateModules(p, deltaTime);
            ++newAliveCount;
        }
    }

    const bool countChanged = (newAliveCount != m_aliveCount);
    m_aliveCount = newAliveCount;
    m_dirty = countChanged || (m_aliveCount > 0); // mark dirty when there's anything to render

    if (spawn && !spawn->loop && m_time >= spawn->duration && m_aliveCount == 0)
        m_playing = false;
}

void ParticleEmitter::setPhysicsScene(PhysicsScene *scene)
{
    for (auto &[_, mod] : m_modules)
        mod->setPhysicsScene(scene);
}

void ParticleEmitter::spawnParticleAt(const glm::vec3 &worldPos, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t idx = allocateParticle();
        if (idx == UINT32_MAX)
            return;

        Particle &p = m_particles[idx];
        p = Particle{};
        p.alive = true;
        p.age   = 0.0f;

        // Apply spawn modules (sets velocity, color, lifetime, size etc.)
        // then override position so shape doesn't overwrite our world pos.
        applySpawnModules(p, worldPos);
        p.position = worldPos;

        ++m_aliveCount;
        m_dirty = true;
    }
}

void ParticleEmitter::play()
{
    m_playing = true;
}

void ParticleEmitter::stop()
{
    m_playing = false;
    reset();
}

void ParticleEmitter::reset()
{
    for (auto &p : m_particles)
        p.alive = false;

    m_freeList.clear();
    for (uint32_t i = 0; i < MAX_PARTICLES; ++i)
        m_freeList.push_back(MAX_PARTICLES - 1 - i);

    m_aliveCount = 0;
    m_spawnAccumulator = 0.0f;
    m_time = 0.0f;
    m_burstFired = false;
    m_dirty = true;
}

uint32_t ParticleEmitter::allocateParticle()
{
    if (m_freeList.empty())
        return UINT32_MAX; // pool full

    const uint32_t idx = m_freeList.back();
    m_freeList.pop_back();
    return idx;
}

void ParticleEmitter::freeParticle(uint32_t index)
{
    m_particles[index].alive = false;
    m_freeList.push_back(index);
}

void ParticleEmitter::spawnParticle(const glm::vec3 &emitterPos)
{
    const uint32_t idx = allocateParticle();
    if (idx == UINT32_MAX)
        return;

    Particle &p = m_particles[idx];
    p = Particle{}; // reset to defaults
    p.alive = true;
    p.age = 0.0f;

    applySpawnModules(p, emitterPos);

    ++m_aliveCount;
    m_dirty = true;
}

void ParticleEmitter::applySpawnModules(Particle &p, const glm::vec3 &emitterPos)
{
    for (auto &[_, mod] : m_modules)
    {
        if (mod->isEnabled())
            mod->onParticleSpawn(p);
    }

    if (!hasModule<SpawnModule>())
        p.position = emitterPos;
}

void ParticleEmitter::applyUpdateModules(Particle &p, float dt)
{
    for (auto &[_, mod] : m_modules)
    {
        if (mod->isEnabled())
            mod->onParticleUpdate(p, dt);
    }
}

ELIX_NESTED_NAMESPACE_END
