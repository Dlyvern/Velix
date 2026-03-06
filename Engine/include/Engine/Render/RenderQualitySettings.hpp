#ifndef ELIX_RENDER_QUALITY_SETTINGS_HPP
#define ELIX_RENDER_QUALITY_SETTINGS_HPP

#include "Core/Macros.hpp"

#include <cstdint>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class RenderQualitySettings
{
public:
    static RenderQualitySettings &getInstance()
    {
        static RenderQualitySettings s_instance;
        return s_instance;
    }

    enum class ShadowQuality : uint32_t
    {
        Low    = 512,
        Medium = 1024,
        High   = 2048,
        Ultra  = 4096
    };

    enum class ShadowCascadeCount : uint8_t
    {
        X1 = 1,
        X2 = 2,
        X4 = 4
    };

    enum class MSAAMode : uint8_t
    {
        OFF = 0,
        X2  = 1,
        X4  = 2,
        X8  = 3,
        X16 = 4
    };

    enum class AnisotropyMode : uint8_t
    {
        OFF = 0,
        X2  = 1,
        X4  = 2,
        X8  = 3,
        X16 = 4
    };

    enum class AntiAliasingMode : uint8_t
    {
        NONE = 0,
        FXAA = 1,
        SMAA = 2,
        TAA  = 3,
        CMAA = 4
    };

    // Shadow
    ShadowQuality shadowQuality{ShadowQuality::High};
    ShadowCascadeCount shadowCascadeCount{ShadowCascadeCount::X4};
    float shadowMaxDistance{180.0f}; // world-space cap for directional shadow cascades

    // Post-processing toggles
    bool enableVSync{false};
    bool enablePostProcessing{true};
    bool enableFXAA{true};
    bool enableBloom{true};
    bool enableSSR{false};

    // Bloom parameters
    float bloomThreshold{0.85f};
    float bloomKnee{0.1f};
    float bloomStrength{0.5f};

    // SSR parameters
    float ssrMaxDistance{25.0f};
    float ssrThickness{0.3f};
    float ssrStrength{1.0f};
    int   ssrSteps{24};

    // Render scale (1.0 = native)
    float renderScale{1.0f};

    // MSAA (deferred GBuffer path)
    MSAAMode msaaMode{MSAAMode::OFF};
    AnisotropyMode anisotropyMode{AnisotropyMode::X16};

    // Textures / mipmapping
    bool  enableTextureMipmaps{true};
    int   textureMipLevelLimit{0};              // 0 = full mip chain
    float textureLodBias{0.0f};                 // sampler bias, higher = blurrier/faster
    float textureLodDistanceStart{8.0f};        // world/view distance where distance mip bias starts
    float textureLodDistanceEnd{60.0f};         // world/view distance where max distance mip bias is reached
    float textureLodDistanceBias{2.0f};         // extra mip bias at far distance
    uint32_t texturePreviewMaxDimension{512u};  // editor preview texture cap (longest side)
    uint32_t textureImportMaxDimension{2048u};  // imported/runtime texture cap (longest side), 0 = unlimited
    bool  enableTextureOomFallback{true};       // fallback to tiny texture when GPU memory allocation fails
    uint32_t textureOomFallbackDimension{10u};  // tiny fallback texture size

    // SSAO / GTAO
    bool  enableSSAO{true};
    float ssaoRadius{0.5f};
    float ssaoBias{0.025f};
    float ssaoStrength{1.2f};
    int   ssaoSamples{32};
    bool  enableGTAO{false};    // higher-quality SSAO variant (more expensive)
    int   gtaoDirections{4};    // [2, 8] — GTAO horizon directions
    int   gtaoSteps{4};         // [2, 8] — GTAO steps per direction
    bool  useBentNormals{false};// requires GTAO; IBL diffuse uses bent normal direction

    // Anisotropic GGX (per-material tangent from GBuffer slot 4)
    bool  enableAnisotropy{false};
    float anisotropyStrength{0.3f};  // [-1, 1]
    float anisotropyRotation{0.0f}; // [0, 360] degrees added on top of per-mesh tangent

    // Shadow on ambient: directional shadow factor scales ambient term
    float shadowAmbientStrength{0.5f}; // [0, 1]
    bool  enableShadowOcclusionCulling{false}; // Experimental: can reduce shadow submissions but may introduce temporal instability.

    // Camera-driven occlusion culling (query-based)
    bool enableOcclusionCulling{true};
    int  occlusionProbeInterval{4};
    int  occlusionVisibleRequeryInterval{12};
    int  occlusionOccludedConfirmationQueries{3};
    int  occlusionMaxInstancesPerBatch{12};
    int  occlusionFastMotionProbeInterval{1};
    int  occlusionFastMotionVisibleRequeryInterval{4};
    int  occlusionFastMotionStaleRevealFrames{0};
    float occlusionFastMotionTranslationThreshold{0.08f};
    float occlusionFastMotionForwardDotThreshold{0.998f};
    int  shadowOcclusionVisibilityGraceFrames{48};

    // LUT-based color grading (applied in tonemap pass, after ACES + gamma)
    bool        enableLUTGrading{false};
    std::string lutGradingPath{};
    float       lutGradingStrength{1.0f}; // [0, 1]

    // TAA (temporal anti-aliasing) — requires velocity buffer
    bool  enableTAA{false};
    float taaHistoryWeight{0.9f};

    // SMAA
    bool  enableSMAA{false}; // replaces FXAA when on
    bool  enableCMAA{false}; // conservative morphological AA (implemented via CMAA preset)

    // Color grading (applied in Tonemap pass)
    bool  enableColorGrading{false};
    float colorGradingSaturation{1.0f};   // 0 = grayscale, 1 = natural, 2 = vivid
    float colorGradingContrast{1.0f};     // 0 = flat, 1 = natural, 2 = high contrast
    float colorGradingTemperature{0.0f};  // -1 = cool/blue, +1 = warm/orange
    float colorGradingTint{0.0f};         // -1 = magenta, +1 = green

    // Contact shadows
    bool  enableContactShadows{false};
    float contactShadowLength{0.5f};      // world-space ray march length
    float contactShadowStrength{0.8f};    // max darkening [0, 1]
    int   contactShadowSteps{16};

    // Cinematic effects
    bool  enableVignette{false};
    float vignetteStrength{0.4f};
    bool  enableFilmGrain{false};
    float filmGrainStrength{0.03f};
    bool  enableChromaticAberration{false};
    float chromaticAberrationStrength{0.003f};

    // IBL (Image-Based Lighting)
    bool  enableIBL{false};
    float iblDiffuseIntensity{1.0f};   // [0, 3]
    float iblSpecularIntensity{0.5f};  // [0, 3]

    AntiAliasingMode getAntiAliasingMode() const
    {
        if (enableTAA)
            return AntiAliasingMode::TAA;

        if (enableCMAA)
            return AntiAliasingMode::CMAA;

        if (enableSMAA)
            return AntiAliasingMode::SMAA;

        if (enableFXAA)
            return AntiAliasingMode::FXAA;

        return AntiAliasingMode::NONE;
    }

    void setAntiAliasingMode(AntiAliasingMode mode)
    {
        enableFXAA = false;
        enableSMAA = false;
        enableTAA = false;
        enableCMAA = false;

        switch (mode)
        {
        case AntiAliasingMode::FXAA:
            enableFXAA = true;
            break;
        case AntiAliasingMode::SMAA:
            enableSMAA = true;
            break;
        case AntiAliasingMode::TAA:
            enableTAA = true;
            break;
        case AntiAliasingMode::CMAA:
            enableCMAA = true;
            break;
        case AntiAliasingMode::NONE:
        default:
            break;
        }
    }

    uint32_t getMSAASampleCount() const
    {
        switch (msaaMode)
        {
        case MSAAMode::X2:
            return 2u;
        case MSAAMode::X4:
            return 4u;
        case MSAAMode::X8:
            return 8u;
        case MSAAMode::X16:
            return 16u;
        case MSAAMode::OFF:
        default:
            return 1u;
        }
    }

    uint32_t getShadowResolution() const
    {
        return static_cast<uint32_t>(shadowQuality);
    }

    uint32_t getShadowCascadeCount() const
    {
        return static_cast<uint32_t>(shadowCascadeCount);
    }

    float getRequestedAnisotropyLevel() const
    {
        switch (anisotropyMode)
        {
        case AnisotropyMode::X2:
            return 2.0f;
        case AnisotropyMode::X4:
            return 4.0f;
        case AnisotropyMode::X8:
            return 8.0f;
        case AnisotropyMode::X16:
            return 16.0f;
        case AnisotropyMode::OFF:
        default:
            return 1.0f;
        }
    }

private:
    RenderQualitySettings() = default;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_QUALITY_SETTINGS_HPP
