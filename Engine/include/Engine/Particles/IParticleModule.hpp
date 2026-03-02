#ifndef ELIX_IPARTICLE_MODULE_HPP
#define ELIX_IPARTICLE_MODULE_HPP

#include "Core/Macros.hpp"

#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct Particle;

enum class ParticleModuleType : uint8_t
{
    Spawn = 0,
    InitialVelocity,
    Lifetime,
    ColorOverLifetime,
    SizeOverLifetime,
    Force,
    Renderer,
    Custom
};

class IParticleModule
{
public:
    virtual ~IParticleModule() = default;

    virtual ParticleModuleType getType() const = 0;

    /// Initialise a freshly-spawned particle.
    virtual void onParticleSpawn(Particle & /*particle*/) {}

    /// Advance a live particle by deltaTime seconds.
    virtual void onParticleUpdate(Particle & /*particle*/, float /*deltaTime*/) {}

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool value) { m_enabled = value; }

protected:
    bool m_enabled{true};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_IPARTICLE_MODULE_HPP
