#include "Engine/Particles/ParticleSystem.hpp"

#include "Engine/Particles/Modules/SpawnModule.hpp"
#include "Engine/Particles/Modules/LifetimeModule.hpp"
#include "Engine/Particles/Modules/InitialVelocityModule.hpp"
#include "Engine/Particles/Modules/ColorOverLifetimeModule.hpp"
#include "Engine/Particles/Modules/SizeOverLifetimeModule.hpp"
#include "Engine/Particles/Modules/ForceModule.hpp"
#include "Engine/Particles/Modules/RendererModule.hpp"
#include "Engine/Particles/Modules/CollisionModule.hpp"

#include <algorithm>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ParticleEmitter *ParticleSystem::addEmitter(const std::string &emitterName)
{
    auto emitter = std::make_unique<ParticleEmitter>(emitterName);
    ParticleEmitter *ptr = emitter.get();
    m_emitters.push_back(std::move(emitter));
    return ptr;
}

ParticleEmitter *ParticleSystem::getEmitter(const std::string &emitterName) const
{
    for (const auto &e : m_emitters)
        if (e->name == emitterName)
            return e.get();
    return nullptr;
}

void ParticleSystem::removeEmitter(const std::string &emitterName)
{
    m_emitters.erase(
        std::remove_if(m_emitters.begin(), m_emitters.end(),
                       [&](const auto &e)
                       { return e->name == emitterName; }),
        m_emitters.end());
}

void ParticleSystem::setPhysicsScene(PhysicsScene *scene)
{
    for (auto &emitter : m_emitters)
        emitter->setPhysicsScene(scene);
}

void ParticleSystem::update(float deltaTime, const glm::vec3 &worldPosition)
{
    if (!m_playing || m_paused)
        return;

    for (auto &emitter : m_emitters)
        emitter->update(deltaTime, worldPosition);

    // Transfer collision hit positions to splash emitters.
    for (auto &emitter : m_emitters)
    {
        auto *collMod = emitter->getModule<CollisionModule>();
        if (!collMod || !collMod->isEnabled())
            continue;

        const auto &hits = collMod->getHitPositions();
        if (hits.empty())
            continue;

        auto *splashEmitter = getEmitter(collMod->splashEmitterName);
        if (splashEmitter)
        {
            for (const auto &hitPos : hits)
                splashEmitter->spawnParticleAt(hitPos, collMod->splashCount);
        }

        collMod->clearHits();
    }

    // Dispatch sub-emitter bursts: for each emitter with subEmitterOnDeath set,
    // find the target emitter by name and spawn a burst at each death position.
    for (auto &emitter : m_emitters)
    {
        auto *spawnMod = emitter->getModule<SpawnModule>();
        if (!spawnMod || spawnMod->subEmitterOnDeath.empty())
            continue;

        const auto &deaths = emitter->getDeathPositions();
        if (deaths.empty())
            continue;

        auto *targetEmitter = getEmitter(spawnMod->subEmitterOnDeath);
        if (targetEmitter)
        {
            const uint32_t burstCount = static_cast<uint32_t>(std::max(1, spawnMod->subEmitterBurstCount));
            for (const auto &deathPos : deaths)
                targetEmitter->spawnParticleAt(deathPos, burstCount);
        }
    }
}

void ParticleSystem::play()
{
    m_playing = true;
    m_paused = false;

    for (auto &emitter : m_emitters)
        emitter->play();
}

void ParticleSystem::stop()
{
    m_playing = false;
    m_paused = false;

    for (auto &emitter : m_emitters)
        emitter->stop();
}

void ParticleSystem::pause()
{
    m_paused = true;
}

void ParticleSystem::reset()
{
    m_playing = false;
    m_paused = false;

    for (auto &emitter : m_emitters)
        emitter->reset();
}

ELIX_NESTED_NAMESPACE_END
