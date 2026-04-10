#ifndef ELIX_RENDER_QUALITY_SETTINGS_HPP
#define ELIX_RENDER_QUALITY_SETTINGS_HPP

#include "Core/Macros.hpp"

#include <cstdint>
#include <volk.h>

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
        Low = 512,
        Medium = 1024,
        High = 2048,
        Ultra = 4096
    };

    enum class ShadowCascadeCount : uint8_t
    {
        X1 = 1,
        X2 = 2,
        X4 = 4
    };

    enum class AnisotropyMode : uint8_t
    {
        OFF = 0,
        X2 = 1,
        X4 = 2,
        X8 = 3,
        X16 = 4
    };

    enum class AntiAliasingMode : uint8_t
    {
        NONE = 0,
        FXAA = 1,
        SMAA = 2,
        TAA = 3,
        CMAA = 4
    };

    enum class MsaaMode : uint8_t
    {
        Off = 0,
        X2 = 1,
        X4 = 2,
        X8 = 3
    };

    enum class RayTracingMode : uint8_t
    {
        Off = 0,
        RayQuery = 1,
        Pipeline = 2
    };

    enum class VolumetricFogQuality : uint8_t
    {
        Off = 0,
        Low = 1,
        High = 2
    };

    ShadowQuality shadowQuality{ShadowQuality::High};
    ShadowCascadeCount shadowCascadeCount{ShadowCascadeCount::X4};
    float shadowMaxDistance{180.0f};

    bool enableVSync{false};
    bool enablePostProcessing{true};
    bool enableRayTracing{false};
    bool enableRTShadows{false};
    bool enableRTReflections{false};
    bool enableRTAO{false};
    float rtaoRadius{1.5f};
    int rtaoSamples{4};
    RayTracingMode rayTracingMode{RayTracingMode::RayQuery};
    int rtShadowSamples{4};            // rays per light: 1=hard, 4=default soft, 16=high quality
    float rtShadowPenumbraSize{0.05f}; // virtual light radius in world units → penumbra width
    int rtReflectionSamples{1};        // rays per pixel: 1=mirror, 4-8=glossy
    float rtRoughnessThreshold{0.4f};  // skip surfaces rougher than this (0=only perfect mirrors, 1=all)
    float rtReflectionStrength{1.0f};  // overall reflection intensity multiplier
    AntiAliasingMode antiAliasingMode{AntiAliasingMode::FXAA};
    MsaaMode msaaMode{MsaaMode::Off};
    bool enableBloom{true};

    float bloomThreshold{0.85f};
    float bloomKnee{0.1f};
    float bloomStrength{0.5f};

    float renderScale{1.0f};
    AnisotropyMode anisotropyMode{AnisotropyMode::X16};

    // Negative values prefer higher-resolution mip levels (sharper at distance, more bandwidth).
    // Positive values prefer lower-resolution mip levels (blurrier, less bandwidth).
    // Range : [ -4, 0 ].
    float textureMipBias{-1.5f};

    bool  enableSSR{false};
    float ssrMaxDistance{15.0f};
    float ssrThickness{0.03f};
    float ssrStrength{1.0f};
    int   ssrSteps{48};
    float ssrRoughnessCutoff{0.4f};
    VolumetricFogQuality volumetricFogQuality{VolumetricFogQuality::High};
    bool overrideVolumetricFogSceneSetting{false};
    bool volumetricFogOverrideEnabled{true};

    bool enableSSAO{true};
    float ssaoRadius{0.5f};
    float ssaoBias{0.025f};
    float ssaoStrength{1.2f};
    int ssaoSamples{32};
    bool enableGTAO{false};
    int gtaoDirections{4};
    int gtaoSteps{4};
    bool useBentNormals{false};

    float shadowAmbientStrength{0.5f};

    bool enableColorGrading{false};
    float colorGradingSaturation{1.0f};
    float colorGradingContrast{1.0f};
    float colorGradingTemperature{0.0f};
    float colorGradingTint{0.0f};

    // Screen-space size culling: skip meshes whose projected bounding sphere
    // is smaller than this radius (in pixels).  2 px = nearly invisible detail.
    bool enableSmallFeatureCulling{true};
    float smallFeatureCullingThreshold{2.0f};

    bool enableContactShadows{false};
    float contactShadowLength{0.5f};
    float contactShadowStrength{0.8f};
    int contactShadowSteps{16};

    bool  enableMotionBlur{false};
    float motionBlurIntensity{1.0f};
    int   motionBlurSamples{16};

    bool enableDecals{true};

    // RT Global Illumination (indirect diffuse)
    bool  enableRTGI{false};
    int   giSamples{1};           // hemisphere rays per pixel: 1=preview, 4=default, 16=high
    bool  enableRTGIDenoiser{true};
    float giStrength{1.0f};       // indirect diffuse intensity multiplier

    // Auto exposure (eye adaptation)
    bool  enableAutoExposure{false};
    float autoExposureSpeedUp{3.0f};    // s⁻¹ — adaptation speed when going darker→brighter
    float autoExposureSpeedDown{1.5f};  // s⁻¹ — adaptation speed when going brighter→darker
    float autoExposureLowPercent{0.10f};  // fraction of darkest pixels to exclude
    float autoExposureHighPercent{0.10f}; // fraction of brightest pixels to exclude

    bool  enableVignette{false};
    float vignetteStrength{0.4f};
    bool enableFilmGrain{false};
    float filmGrainStrength{0.03f};
    bool enableChromaticAberration{false};
    float chromaticAberrationStrength{0.003f};

    AntiAliasingMode getAntiAliasingMode() const
    {
        return antiAliasingMode;
    }

    void setAntiAliasingMode(AntiAliasingMode mode)
    {
        antiAliasingMode = mode;
    }

    void setMsaaMode(MsaaMode mode)
    {
        msaaMode = mode;
    }

    VkSampleCountFlagBits getRequestedMsaaSampleCount() const
    {
        switch (msaaMode)
        {
        case MsaaMode::X2:
            return VK_SAMPLE_COUNT_2_BIT;
        case MsaaMode::X4:
            return VK_SAMPLE_COUNT_4_BIT;
        case MsaaMode::X8:
            return VK_SAMPLE_COUNT_8_BIT;
        case MsaaMode::Off:
        default:
            return VK_SAMPLE_COUNT_1_BIT;
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
