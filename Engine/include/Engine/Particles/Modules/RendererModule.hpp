#ifndef ELIX_RENDERER_MODULE_HPP
#define ELIX_RENDERER_MODULE_HPP

#include "Engine/Particles/IParticleModule.hpp"

#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

enum class ParticleBlendMode : uint8_t
{
    AlphaBlend = 0, // standard transparency
    Additive,       // glow / fire / sparks
    Premultiplied,  // pre-multiplied alpha
};

enum class ParticleFacingMode : uint8_t
{
    CameraFacing = 0,   // always billboards toward the camera
    VelocityAligned,    // long axis aligns with velocity (good for rain streaks)
    WorldUp,            // locked to world Y-up
};

/// Purely descriptive module — carries settings read by ParticleRenderGraphPass.
/// Has no per-particle simulation logic.
class RendererModule final : public IParticleModule
{
public:
    ParticleModuleType getType() const override { return ParticleModuleType::Renderer; }

    std::string        texturePath{};                              // empty = solid colour quad
    ParticleBlendMode  blendMode{ParticleBlendMode::AlphaBlend};
    ParticleFacingMode facingMode{ParticleFacingMode::CameraFacing};

    bool  castShadows{false};
    bool  softParticles{false};       // depth-buffer soft-edge blending
    float softParticleRange{1.0f};    // world-space soft-edge falloff distance
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDERER_MODULE_HPP
