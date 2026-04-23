#ifndef ELIX_RENDERER_MODULE_HPP
#define ELIX_RENDERER_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"

#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

enum class ParticleBlendMode : uint8_t
{
    AlphaBlend = 0,
    Additive,
    Premultiplied,
};

enum class ParticleFacingMode : uint8_t
{
    CameraFacing = 0,
    VelocityAligned,
    WorldUp,
};



class RendererModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::Renderer; }

    std::string        texturePath{};
    ParticleBlendMode  blendMode{ParticleBlendMode::AlphaBlend};
    ParticleFacingMode facingMode{ParticleFacingMode::CameraFacing};

    bool  castShadows{false};
    bool  softParticles{false};
    float softParticleRange{1.0f};
};

ELIX_NESTED_NAMESPACE_END

#endif
