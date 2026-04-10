#ifndef ELIX_PROJECT_CONFIG_HPP
#define ELIX_PROJECT_CONFIG_HPP

#include "Core/Macros.hpp"

#include <cstdint>
#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ProjectConfig
{
public:
    struct CameraSettings
    {
        float moveSpeed{3.0f};
        float mouseSensitivity{0.1f};
        uint8_t projectionMode{0};
        float nearPlane{0.1f};
        float farPlane{1000.0f};
        float fov{60.0f};
        float orthographicSize{10.0f};
        float positionX{0.0f};
        float positionY{1.0f};
        float positionZ{5.0f};
        float yaw{0.0f};
        float pitch{0.0f};
    };

    bool load(const std::filesystem::path &projectRoot);
    bool save(const std::filesystem::path &projectRoot) const;
    void applyRenderSettings() const;
    void captureRenderSettings();

    const CameraSettings &getCameraSettings() const { return m_cameraSettings; }
    void setCameraSettings(const CameraSettings &settings) { m_cameraSettings = settings; }

private:
    CameraSettings m_cameraSettings{};

    uint32_t m_shadowQuality{2048};
    uint32_t m_shadowCascadeCount{4};
    float m_shadowMaxDistance{180.0f};

    int m_antiAliasingMode{1};
    int m_msaaMode{0};

    bool m_enablePostProcessing{true};
    bool m_enableVSync{false};
    bool m_enableRayTracing{false};
    bool m_enableRTShadows{false};
    bool m_enableRTReflections{false};
    int m_rayTracingMode{1};
    float m_renderScale{1.0f};
    int m_anisotropyMode{4};

    bool m_enableSSAO{true};
    float m_ssaoRadius{0.5f};
    float m_ssaoBias{0.025f};
    float m_ssaoStrength{1.2f};
    int m_ssaoSamples{32};
    bool m_enableGTAO{false};
    int m_gtaoDirections{4};
    int m_gtaoSteps{4};
    bool m_useBentNormals{false};

    bool m_enableSSR{false};
    float m_ssrMaxDistance{15.0f};
    float m_ssrThickness{0.03f};
    float m_ssrStrength{1.0f};
    int m_ssrSteps{48};
    float m_ssrRoughnessCutoff{0.4f};
    int m_volumetricFogQuality{2};
    bool m_overrideVolumetricFogSceneSetting{false};
    bool m_volumetricFogOverrideEnabled{true};

    float m_shadowAmbientStrength{0.5f};

    bool m_enableBloom{true};
    float m_bloomThreshold{0.85f};
    float m_bloomKnee{0.1f};
    float m_bloomStrength{0.5f};

    bool m_enableContactShadows{false};
    float m_contactShadowLength{0.5f};
    float m_contactShadowStrength{0.8f};
    int m_contactShadowSteps{16};

    bool m_enableColorGrading{false};
    float m_colorGradingSaturation{1.0f};
    float m_colorGradingContrast{1.0f};
    float m_colorGradingTemperature{0.0f};
    float m_colorGradingTint{0.0f};

    bool m_enableVignette{false};
    float m_vignetteStrength{0.4f};
    bool m_enableFilmGrain{false};
    float m_filmGrainStrength{0.03f};
    bool m_enableChromaticAberration{false};
    float m_chromaticAberrationStrength{0.003f};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PROJECT_CONFIG_HPP
