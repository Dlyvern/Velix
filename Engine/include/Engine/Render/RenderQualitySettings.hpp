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
        Low    = 512,
        Medium = 1024,
        High   = 2048,
        Ultra  = 4096
    };

    // Shadow
    ShadowQuality shadowQuality{ShadowQuality::High};

    // Post-processing toggles
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

    // SSAO / GTAO
    bool  enableSSAO{true};
    float ssaoRadius{0.5f};
    float ssaoBias{0.025f};
    float ssaoStrength{1.2f};
    int   ssaoSamples{32};
    bool  enableGTAO{false}; // higher-quality SSAO variant (more expensive)

    // TAA (temporal anti-aliasing) — requires velocity buffer
    bool  enableTAA{false};
    float taaHistoryWeight{0.9f};

    // SMAA
    bool  enableSMAA{false}; // replaces FXAA when on

    uint32_t getShadowResolution() const
    {
        return static_cast<uint32_t>(shadowQuality);
    }

private:
    RenderQualitySettings() = default;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_QUALITY_SETTINGS_HPP
