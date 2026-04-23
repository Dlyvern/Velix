#ifndef ELIX_IPARTICLE_MODULE_HPP
#define ELIX_IPARTICLE_MODULE_HPP

#include "Core/Macros.hpp"

#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct Particle;
class PhysicsScene;

enum class ParticleModuleType : uint8_t
{
    Spawn = 0,
    InitialVelocity,
    Lifetime,
    ColorOverLifetime,
    SizeOverLifetime,
    Force,
    Renderer,
    Collision,
    VelocityOverLifetime,
    RotationOverLifetime,
    Turbulence,
    Custom
};

class IParticleModule
{
public:
    virtual ~IParticleModule() = default;

    virtual ParticleModuleType getType() const = 0;

    virtual void onParticleSpawn(Particle & ) {}

    virtual void onParticleUpdate(Particle & , float ) {}

    virtual void setPhysicsScene(PhysicsScene * ) {}

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool value) { m_enabled = value; }

protected:
    bool m_enabled{true};
};

ELIX_NESTED_NAMESPACE_END

#endif
