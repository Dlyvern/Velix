#ifndef ELIX_RENDER_QUALITY_SETTINGS_HPP
#define ELIX_RENDER_QUALITY_SETTINGS_HPP

#include "Core/Macros.hpp"

#include <cstdint>

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

    ShadowQuality shadowQuality{ShadowQuality::High};
    ShadowCascadeCount shadowCascadeCount{ShadowCascadeCount::X4};
    float shadowMaxDistance{180.0f};

    bool enableVSync{false};
    bool enablePostProcessing{true};
    bool enableFXAA{true};
    bool enableBloom{true};

    float bloomThreshold{0.85f};
    float bloomKnee{0.1f};
    float bloomStrength{0.5f};

    float renderScale{1.0f};
    AnisotropyMode anisotropyMode{AnisotropyMode::X16};

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

    bool enableTAA{false};
    float taaHistoryWeight{0.9f};

    bool enableSMAA{false};
    bool enableCMAA{false};

    bool enableColorGrading{false};
    float colorGradingSaturation{1.0f};
    float colorGradingContrast{1.0f};
    float colorGradingTemperature{0.0f};
    float colorGradingTint{0.0f};

    bool enableContactShadows{false};
    float contactShadowLength{0.5f};
    float contactShadowStrength{0.8f};
    int contactShadowSteps{16};

    bool enableVignette{false};
    float vignetteStrength{0.4f};
    bool enableFilmGrain{false};
    float filmGrainStrength{0.03f};
    bool enableChromaticAberration{false};
    float chromaticAberrationStrength{0.003f};

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
