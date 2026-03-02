#include "Engine/Particles/ParticleSystem.hpp"

#include "Engine/Particles/Modules/SpawnModule.hpp"
#include "Engine/Particles/Modules/LifetimeModule.hpp"
#include "Engine/Particles/Modules/InitialVelocityModule.hpp"
#include "Engine/Particles/Modules/ColorOverLifetimeModule.hpp"
#include "Engine/Particles/Modules/SizeOverLifetimeModule.hpp"
#include "Engine/Particles/Modules/ForceModule.hpp"
#include "Engine/Particles/Modules/RendererModule.hpp"

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

void ParticleSystem::update(float deltaTime, const glm::vec3 &worldPosition)
{
    if (!m_playing || m_paused)
        return;

    for (auto &emitter : m_emitters)
        emitter->update(deltaTime, worldPosition);
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

ParticleSystem::SharedPtr ParticleSystem::createRain()
{
    auto system = std::make_shared<ParticleSystem>();
    system->name = "Rain";

    auto *emitter = system->addEmitter("Rain Drops");

    auto *spawn = emitter->addModule<SpawnModule>();
    spawn->spawnRate = 600.0f;
    spawn->burstCount = 0.0f;
    spawn->loop = true;

    spawn->shape.shape = EmitterShape::Box;
    spawn->shape.extents = glm::vec3(25.0f, 0.0f, 25.0f); // flat spawn plane

    auto *lifetime = emitter->addModule<LifetimeModule>();
    lifetime->minLifetime = 1.8f;
    lifetime->maxLifetime = 3.0f;

    auto *vel = emitter->addModule<InitialVelocityModule>();
    vel->baseVelocity = glm::vec3(0.0f, -14.0f, 0.0f); // straight down, fast
    vel->randomness = glm::vec3(0.6f, 1.5f, 0.6f);     // slight horizontal scatter

    auto *size = emitter->addModule<SizeOverLifetimeModule>();
    size->baseSize = glm::vec2(0.02f, 0.18f); // thin elongated streak
    size->curve = {{0.0f, 1.0f}, {0.7f, 1.0f}, {1.0f, 0.6f}};

    auto *col = emitter->addModule<ColorOverLifetimeModule>();
    col->gradient = {
        {0.0f, glm::vec4(0.75f, 0.88f, 1.0f, 0.75f)}, // light blue, semi-transparent
        {0.7f, glm::vec4(0.70f, 0.85f, 1.0f, 0.50f)}, // slightly more transparent
        {1.0f, glm::vec4(0.65f, 0.82f, 1.0f, 0.00f)}, // fully transparent at death
    };

    auto *force = emitter->addModule<ForceModule>();
    force->force = glm::vec3(0.0f, -2.0f, 0.0f); // gentle extra gravity (drops already fast)
    force->drag = 0.02f;

    auto *renderer = emitter->addModule<RendererModule>();
    renderer->blendMode = ParticleBlendMode::AlphaBlend;
    renderer->facingMode = ParticleFacingMode::VelocityAligned; // streaks align with fall

    return system;
}

ELIX_NESTED_NAMESPACE_END
