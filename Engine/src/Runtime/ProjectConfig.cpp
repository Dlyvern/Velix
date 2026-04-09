#include "Engine/Runtime/ProjectConfig.hpp"

#include "Core/Logger.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace
{
    constexpr const char *k_filename = "project.settings";
}

bool ProjectConfig::load(const std::filesystem::path &projectRoot)
{
    const std::filesystem::path path = projectRoot / k_filename;
    if (!std::filesystem::exists(path))
        return true;

    std::ifstream file(path);
    if (!file.is_open())
    {
        VX_ENGINE_WARNING_STREAM("Failed to open project config: " << path << '\n');
        return false;
    }

    nlohmann::json json;
    try
    {
        file >> json;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        VX_ENGINE_WARNING_STREAM("Failed to parse project config: " << e.what() << '\n');
        return false;
    }

    if (json.contains("camera") && json["camera"].is_object())
    {
        const auto &c = json["camera"];
        auto &s = m_cameraSettings;
#define READ_FLOAT(key, field)                 \
    if (c.contains(key) && c[key].is_number()) \
    s.field = c[key].get<float>()
#define READ_UINT8(key, field)                         \
    if (c.contains(key) && c[key].is_number_integer()) \
    s.field = static_cast<uint8_t>(c[key].get<int>())
        READ_FLOAT("move_speed", moveSpeed);
        READ_FLOAT("mouse_sensitivity", mouseSensitivity);
        READ_UINT8("projection_mode", projectionMode);
        READ_FLOAT("near_plane", nearPlane);
        READ_FLOAT("far_plane", farPlane);
        READ_FLOAT("fov", fov);
        READ_FLOAT("orthographic_size", orthographicSize);
        READ_FLOAT("position_x", positionX);
        READ_FLOAT("position_y", positionY);
        READ_FLOAT("position_z", positionZ);
        READ_FLOAT("yaw", yaw);
        READ_FLOAT("pitch", pitch);
#undef READ_FLOAT
#undef READ_UINT8
    }

    if (!json.contains("render_settings") || !json["render_settings"].is_object())
        return true;

    const auto &r = json["render_settings"];
#define RF(key, field)                         \
    if (r.contains(key) && r[key].is_number()) \
    field = r[key].get<float>()
#define RB(key, field)                          \
    if (r.contains(key) && r[key].is_boolean()) \
    field = r[key].get<bool>()
#define RI(key, field)                                 \
    if (r.contains(key) && r[key].is_number_integer()) \
    field = r[key].get<int>()
#define RU(key, field)                                  \
    if (r.contains(key) && r[key].is_number_unsigned()) \
    field = r[key].get<uint32_t>()

    if (r.contains("shadow_quality") && r["shadow_quality"].is_number_unsigned())
        m_shadowQuality = r["shadow_quality"].get<uint32_t>();
    if (r.contains("shadow_cascade_count") && r["shadow_cascade_count"].is_number_unsigned())
        m_shadowCascadeCount = r["shadow_cascade_count"].get<uint32_t>();
    RF("shadow_max_distance", m_shadowMaxDistance);

    RB("enable_fxaa", m_enableFXAA);
    RB("enable_smaa", m_enableSMAA);
    RB("enable_taa", m_enableTAA);
    RB("enable_cmaa", m_enableCMAA);
    RI("msaa_mode", m_msaaMode);

    RB("enable_post_processing", m_enablePostProcessing);
    RB("enable_vsync", m_enableVSync);
    RB("enable_ray_tracing", m_enableRayTracing);
    RB("enable_rt_shadows", m_enableRTShadows);
    RB("enable_rt_reflections", m_enableRTReflections);
    RI("ray_tracing_mode", m_rayTracingMode);
    RF("render_scale", m_renderScale);
    RI("anisotropy_mode", m_anisotropyMode);

    RB("enable_ssao", m_enableSSAO);
    RF("ssao_radius", m_ssaoRadius);
    RF("ssao_bias", m_ssaoBias);
    RF("ssao_strength", m_ssaoStrength);
    RI("ssao_samples", m_ssaoSamples);
    RB("enable_gtao", m_enableGTAO);
    RI("gtao_directions", m_gtaoDirections);
    RI("gtao_steps", m_gtaoSteps);
    RB("use_bent_normals", m_useBentNormals);

    RB("enable_ssr", m_enableSSR);
    RF("ssr_max_distance", m_ssrMaxDistance);
    RF("ssr_thickness", m_ssrThickness);
    RF("ssr_strength", m_ssrStrength);
    RI("ssr_steps", m_ssrSteps);
    RF("ssr_roughness_cutoff", m_ssrRoughnessCutoff);
    RI("volumetric_fog_quality", m_volumetricFogQuality);
    RB("override_volumetric_fog_scene_setting", m_overrideVolumetricFogSceneSetting);
    RB("volumetric_fog_override_enabled", m_volumetricFogOverrideEnabled);

    RF("shadow_ambient_strength", m_shadowAmbientStrength);

    RB("enable_bloom", m_enableBloom);
    RF("bloom_threshold", m_bloomThreshold);
    RF("bloom_knee", m_bloomKnee);
    RF("bloom_strength", m_bloomStrength);

    RB("enable_contact_shadows", m_enableContactShadows);
    RF("contact_shadow_length", m_contactShadowLength);
    RF("contact_shadow_strength", m_contactShadowStrength);
    RI("contact_shadow_steps", m_contactShadowSteps);

    RB("enable_color_grading", m_enableColorGrading);
    RF("color_grading_saturation", m_colorGradingSaturation);
    RF("color_grading_contrast", m_colorGradingContrast);
    RF("color_grading_temperature", m_colorGradingTemperature);
    RF("color_grading_tint", m_colorGradingTint);

    RB("enable_vignette", m_enableVignette);
    RF("vignette_strength", m_vignetteStrength);
    RB("enable_film_grain", m_enableFilmGrain);
    RF("film_grain_strength", m_filmGrainStrength);
    RB("enable_chromatic_aberration", m_enableChromaticAberration);
    RF("chromatic_aberration_strength", m_chromaticAberrationStrength);

#undef RF
#undef RB
#undef RI
#undef RU

    return true;
}

bool ProjectConfig::save(const std::filesystem::path &projectRoot) const
{
    if (projectRoot.empty())
        return false;

    try
    {
        std::filesystem::create_directories(projectRoot);
    }
    catch (const std::exception &e)
    {
        VX_ENGINE_ERROR_STREAM("Failed to create project directory: " << e.what() << '\n');
        return false;
    }

    nlohmann::json json;
    const auto &camera = m_cameraSettings;
    json["camera"] = {
        {"move_speed", camera.moveSpeed},
        {"mouse_sensitivity", camera.mouseSensitivity},
        {"projection_mode", camera.projectionMode},
        {"near_plane", camera.nearPlane},
        {"far_plane", camera.farPlane},
        {"fov", camera.fov},
        {"orthographic_size", camera.orthographicSize},
        {"position_x", camera.positionX},
        {"position_y", camera.positionY},
        {"position_z", camera.positionZ},
        {"yaw", camera.yaw},
        {"pitch", camera.pitch}};

    json["render_settings"] = {
        {"shadow_quality", m_shadowQuality},
        {"shadow_cascade_count", m_shadowCascadeCount},
        {"shadow_max_distance", m_shadowMaxDistance},
        {"enable_fxaa", m_enableFXAA},
        {"enable_smaa", m_enableSMAA},
        {"enable_taa", m_enableTAA},
        {"enable_cmaa", m_enableCMAA},
        {"msaa_mode", m_msaaMode},
        {"enable_post_processing", m_enablePostProcessing},
        {"enable_vsync", m_enableVSync},
        {"enable_ray_tracing", m_enableRayTracing},
        {"enable_rt_shadows", m_enableRTShadows},
        {"enable_rt_reflections", m_enableRTReflections},
        {"ray_tracing_mode", m_rayTracingMode},
        {"render_scale", m_renderScale},
        {"anisotropy_mode", m_anisotropyMode},
        {"enable_ssao", m_enableSSAO},
        {"ssao_radius", m_ssaoRadius},
        {"ssao_bias", m_ssaoBias},
        {"ssao_strength", m_ssaoStrength},
        {"ssao_samples", m_ssaoSamples},
        {"enable_gtao", m_enableGTAO},
        {"gtao_directions", m_gtaoDirections},
        {"gtao_steps", m_gtaoSteps},
        {"use_bent_normals", m_useBentNormals},
        {"enable_ssr", m_enableSSR},
        {"ssr_max_distance", m_ssrMaxDistance},
        {"ssr_thickness", m_ssrThickness},
        {"ssr_strength", m_ssrStrength},
        {"ssr_steps", m_ssrSteps},
        {"ssr_roughness_cutoff", m_ssrRoughnessCutoff},
        {"volumetric_fog_quality", m_volumetricFogQuality},
        {"override_volumetric_fog_scene_setting", m_overrideVolumetricFogSceneSetting},
        {"volumetric_fog_override_enabled", m_volumetricFogOverrideEnabled},
        {"shadow_ambient_strength", m_shadowAmbientStrength},
        {"enable_bloom", m_enableBloom},
        {"bloom_threshold", m_bloomThreshold},
        {"bloom_knee", m_bloomKnee},
        {"bloom_strength", m_bloomStrength},
        {"enable_contact_shadows", m_enableContactShadows},
        {"contact_shadow_length", m_contactShadowLength},
        {"contact_shadow_strength", m_contactShadowStrength},
        {"contact_shadow_steps", m_contactShadowSteps},
        {"enable_color_grading", m_enableColorGrading},
        {"color_grading_saturation", m_colorGradingSaturation},
        {"color_grading_contrast", m_colorGradingContrast},
        {"color_grading_temperature", m_colorGradingTemperature},
        {"color_grading_tint", m_colorGradingTint},
        {"enable_vignette", m_enableVignette},
        {"vignette_strength", m_vignetteStrength},
        {"enable_film_grain", m_enableFilmGrain},
        {"film_grain_strength", m_filmGrainStrength},
        {"enable_chromatic_aberration", m_enableChromaticAberration},
        {"chromatic_aberration_strength", m_chromaticAberrationStrength}};

    const std::filesystem::path path = projectRoot / k_filename;
    std::ofstream file(path);
    if (!file.is_open())
    {
        VX_ENGINE_ERROR_STREAM("Failed to write project config: " << path << '\n');
        return false;
    }

    file << std::setw(4) << json << '\n';
    return file.good();
}

void ProjectConfig::applyRenderSettings() const
{
    auto &rs = RenderQualitySettings::getInstance();

    switch (m_shadowQuality)
    {
    case 512u:
        rs.shadowQuality = RenderQualitySettings::ShadowQuality::Low;
        break;
    case 1024u:
        rs.shadowQuality = RenderQualitySettings::ShadowQuality::Medium;
        break;
    case 2048u:
        rs.shadowQuality = RenderQualitySettings::ShadowQuality::High;
        break;
    case 4096u:
        rs.shadowQuality = RenderQualitySettings::ShadowQuality::Ultra;
        break;
    default:
        break;
    }

    switch (m_shadowCascadeCount)
    {
    case 1u:
        rs.shadowCascadeCount = RenderQualitySettings::ShadowCascadeCount::X1;
        break;
    case 2u:
        rs.shadowCascadeCount = RenderQualitySettings::ShadowCascadeCount::X2;
        break;
    case 4u:
        rs.shadowCascadeCount = RenderQualitySettings::ShadowCascadeCount::X4;
        break;
    default:
        break;
    }

    rs.shadowMaxDistance = std::max(m_shadowMaxDistance, 20.0f);
    rs.enableFXAA = m_enableFXAA;
    rs.enableSMAA = m_enableSMAA;
    rs.enableTAA = m_enableTAA;
    rs.enableCMAA = m_enableCMAA;
    rs.msaaMode = static_cast<RenderQualitySettings::MsaaMode>(std::clamp(m_msaaMode, 0, 3));
    rs.enablePostProcessing = m_enablePostProcessing;
    rs.enableVSync = m_enableVSync;
    rs.enableRayTracing = m_enableRayTracing;
    rs.enableRTShadows = m_enableRTShadows;
    rs.enableRTReflections = m_enableRTReflections;
    rs.rayTracingMode = static_cast<RenderQualitySettings::RayTracingMode>(std::clamp(m_rayTracingMode, 0, 2));
    rs.renderScale = std::clamp(m_renderScale, 0.25f, 2.0f);
    rs.anisotropyMode = static_cast<RenderQualitySettings::AnisotropyMode>(std::clamp(m_anisotropyMode, 0, 4));

    rs.enableSSAO = m_enableSSAO;
    rs.ssaoRadius = m_ssaoRadius;
    rs.ssaoBias = m_ssaoBias;
    rs.ssaoStrength = m_ssaoStrength;
    rs.ssaoSamples = std::clamp(m_ssaoSamples, 4, 64);
    rs.enableGTAO = m_enableGTAO;
    rs.gtaoDirections = std::clamp(m_gtaoDirections, 2, 8);
    rs.gtaoSteps = std::clamp(m_gtaoSteps, 2, 8);
    rs.useBentNormals = m_useBentNormals;

    rs.enableSSR = m_enableSSR;
    rs.ssrMaxDistance = std::clamp(m_ssrMaxDistance, 1.0f, 50.0f);
    rs.ssrThickness = std::clamp(m_ssrThickness, 0.005f, 0.25f);
    rs.ssrStrength = std::clamp(m_ssrStrength, 0.0f, 1.0f);
    rs.ssrSteps = std::clamp(m_ssrSteps, 8, 256);
    rs.ssrRoughnessCutoff = std::clamp(m_ssrRoughnessCutoff, 0.05f, 0.8f);
    rs.volumetricFogQuality = static_cast<RenderQualitySettings::VolumetricFogQuality>(std::clamp(m_volumetricFogQuality, 0, 2));
    rs.overrideVolumetricFogSceneSetting = m_overrideVolumetricFogSceneSetting;
    rs.volumetricFogOverrideEnabled = m_volumetricFogOverrideEnabled;

    rs.shadowAmbientStrength = std::clamp(m_shadowAmbientStrength, 0.0f, 1.0f);
    rs.enableBloom = m_enableBloom;
    rs.bloomThreshold = std::clamp(m_bloomThreshold, 0.0f, 5.0f);
    rs.bloomKnee = std::clamp(m_bloomKnee, 0.0f, 1.0f);
    rs.bloomStrength = std::clamp(m_bloomStrength, 0.0f, 2.0f);

    rs.enableContactShadows = m_enableContactShadows;
    rs.contactShadowLength = std::clamp(m_contactShadowLength, 0.1f, 5.0f);
    rs.contactShadowStrength = std::clamp(m_contactShadowStrength, 0.0f, 1.0f);
    rs.contactShadowSteps = std::clamp(m_contactShadowSteps, 4, 32);

    rs.enableColorGrading = m_enableColorGrading;
    rs.colorGradingSaturation = std::clamp(m_colorGradingSaturation, 0.0f, 2.0f);
    rs.colorGradingContrast = std::clamp(m_colorGradingContrast, 0.0f, 2.0f);
    rs.colorGradingTemperature = std::clamp(m_colorGradingTemperature, -1.0f, 1.0f);
    rs.colorGradingTint = std::clamp(m_colorGradingTint, -1.0f, 1.0f);

    rs.enableVignette = m_enableVignette;
    rs.vignetteStrength = std::clamp(m_vignetteStrength, 0.0f, 1.0f);
    rs.enableFilmGrain = m_enableFilmGrain;
    rs.filmGrainStrength = std::clamp(m_filmGrainStrength, 0.0f, 0.2f);
    rs.enableChromaticAberration = m_enableChromaticAberration;
    rs.chromaticAberrationStrength = std::clamp(m_chromaticAberrationStrength, 0.0f, 0.02f);
}

void ProjectConfig::captureRenderSettings()
{
    const auto &rs = RenderQualitySettings::getInstance();

    m_shadowQuality = rs.getShadowResolution();
    m_shadowCascadeCount = rs.getShadowCascadeCount();
    m_shadowMaxDistance = rs.shadowMaxDistance;

    m_enableFXAA = rs.enableFXAA;
    m_enableSMAA = rs.enableSMAA;
    m_enableTAA = rs.enableTAA;
    m_enableCMAA = rs.enableCMAA;
    m_msaaMode = static_cast<int>(rs.msaaMode);

    m_enablePostProcessing = rs.enablePostProcessing;
    m_enableVSync = rs.enableVSync;
    m_enableRayTracing = rs.enableRayTracing;
    m_enableRTShadows = rs.enableRTShadows;
    m_enableRTReflections = rs.enableRTReflections;
    m_rayTracingMode = static_cast<int>(rs.rayTracingMode);
    m_renderScale = rs.renderScale;
    m_anisotropyMode = static_cast<int>(rs.anisotropyMode);

    m_enableSSAO = rs.enableSSAO;
    m_ssaoRadius = rs.ssaoRadius;
    m_ssaoBias = rs.ssaoBias;
    m_ssaoStrength = rs.ssaoStrength;
    m_ssaoSamples = rs.ssaoSamples;
    m_enableGTAO = rs.enableGTAO;
    m_gtaoDirections = rs.gtaoDirections;
    m_gtaoSteps = rs.gtaoSteps;
    m_useBentNormals = rs.useBentNormals;

    m_enableSSR = rs.enableSSR;
    m_ssrMaxDistance = rs.ssrMaxDistance;
    m_ssrThickness = rs.ssrThickness;
    m_ssrStrength = rs.ssrStrength;
    m_ssrSteps = rs.ssrSteps;
    m_ssrRoughnessCutoff = rs.ssrRoughnessCutoff;
    m_volumetricFogQuality = static_cast<int>(rs.volumetricFogQuality);
    m_overrideVolumetricFogSceneSetting = rs.overrideVolumetricFogSceneSetting;
    m_volumetricFogOverrideEnabled = rs.volumetricFogOverrideEnabled;

    m_shadowAmbientStrength = rs.shadowAmbientStrength;
    m_enableBloom = rs.enableBloom;
    m_bloomThreshold = rs.bloomThreshold;
    m_bloomKnee = rs.bloomKnee;
    m_bloomStrength = rs.bloomStrength;

    m_enableContactShadows = rs.enableContactShadows;
    m_contactShadowLength = rs.contactShadowLength;
    m_contactShadowStrength = rs.contactShadowStrength;
    m_contactShadowSteps = rs.contactShadowSteps;

    m_enableColorGrading = rs.enableColorGrading;
    m_colorGradingSaturation = rs.colorGradingSaturation;
    m_colorGradingContrast = rs.colorGradingContrast;
    m_colorGradingTemperature = rs.colorGradingTemperature;
    m_colorGradingTint = rs.colorGradingTint;

    m_enableVignette = rs.enableVignette;
    m_vignetteStrength = rs.vignetteStrength;
    m_enableFilmGrain = rs.enableFilmGrain;
    m_filmGrainStrength = rs.filmGrainStrength;
    m_enableChromaticAberration = rs.enableChromaticAberration;
    m_chromaticAberrationStrength = rs.chromaticAberrationStrength;
}

ELIX_NESTED_NAMESPACE_END
