#include "Engine/Runtime/EngineConfig.hpp"

#include "Core/Logger.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <unordered_set>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace
{
    struct IdeDefinition
    {
        std::string id;
        std::string displayName;
        std::vector<std::string> commands;
    };

    const std::vector<IdeDefinition> &getIdeDefinitions()
    {
        static const std::vector<IdeDefinition> definitions = {
            {"vscode", "Visual Studio Code", {"code"}},
            {"vscode_insiders", "Visual Studio Code Insiders", {"code-insiders"}},
            {"vscodium", "VSCodium", {"codium"}},
            {"clion", "CLion", {"clion", "clion.sh"}},
            {"rider", "Rider", {"rider", "rider.sh"}}};

        return definitions;
    }

    bool isVSCodeIdeId(const std::string &ideId)
    {
        return ideId == "vscode" || ideId == "vscode_insiders" || ideId == "vscodium";
    }

    std::vector<std::filesystem::path> splitPathEnvironment(const std::string &pathEnvironment)
    {
        std::vector<std::filesystem::path> paths;
        const char delimiter =
#if defined(_WIN32)
            ';';
#else
            ':';
#endif

        size_t startIndex = 0;
        while (startIndex <= pathEnvironment.size())
        {
            const size_t endIndex = pathEnvironment.find(delimiter, startIndex);
            const size_t segmentSize = (endIndex == std::string::npos)
                                           ? (pathEnvironment.size() - startIndex)
                                           : (endIndex - startIndex);

            if (segmentSize > 0)
                paths.emplace_back(pathEnvironment.substr(startIndex, segmentSize));

            if (endIndex == std::string::npos)
                break;

            startIndex = endIndex + 1;
        }

        return paths;
    }

    bool isExecutableFile(const std::filesystem::path &path)
    {
        if (path.empty() || !std::filesystem::exists(path) || !std::filesystem::is_regular_file(path))
            return false;

#if defined(_WIN32)
        return _access(path.string().c_str(), 0) == 0;
#else
        return access(path.c_str(), X_OK) == 0;
#endif
    }

    std::optional<std::filesystem::path> findExecutableInPath(const std::string &command)
    {
        if (command.empty())
            return std::nullopt;

        const std::filesystem::path commandPath(command);
        if (commandPath.is_absolute() && isExecutableFile(commandPath))
            return commandPath;

        const char *pathEnvironment = std::getenv("PATH");
        if (!pathEnvironment || std::string(pathEnvironment).empty())
            return std::nullopt;

        const auto pathEntries = splitPathEnvironment(pathEnvironment);
        for (const auto &entry : pathEntries)
        {
            if (entry.empty() || !std::filesystem::exists(entry))
                continue;

            std::filesystem::path candidate = entry / command;
            if (isExecutableFile(candidate))
                return candidate;

#if defined(_WIN32)
            if (!candidate.has_extension())
            {
                static const std::array<std::string, 4> windowsExtensions = {".exe", ".cmd", ".bat", ".com"};
                for (const auto &extension : windowsExtensions)
                {
                    const std::filesystem::path candidateWithExtension = candidate;
                    const std::filesystem::path fullPath = candidateWithExtension.string() + extension;
                    if (isExecutableFile(fullPath))
                        return fullPath;
                }
            }
#endif
        }

        return std::nullopt;
    }
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)

EngineConfig &EngineConfig::instance()
{
    static EngineConfig config;
    return config;
}

bool EngineConfig::reload()
{
    applyDefaults();

    bool loadOk = loadFromDisk();
    detectInstalledIdes();

    if (!isKnownIdeId(m_preferredIdeId))
        m_preferredIdeId = "vscode";

    if (!findIde(m_preferredIdeId).has_value())
    {
        if (auto preferredVSCode = findPreferredVSCodeIde())
            m_preferredIdeId = preferredVSCode->id;
        else if (!m_detectedIdes.empty())
            m_preferredIdeId = m_detectedIdes.front().id;
    }

    if (!std::filesystem::exists(m_configFilePath))
    {
        if (!save())
            return false;
    }

    AssetsLoader::setTextureImportMaxDimension(RenderQualitySettings::getInstance().textureImportMaxDimension);

    return loadOk;
}

bool EngineConfig::save() const
{
    try
    {
        if (!m_configDirectory.empty())
            std::filesystem::create_directories(m_configDirectory);
    }
    catch (const std::exception &exception)
    {
        VX_ENGINE_ERROR_STREAM("Failed to create config directory '" << m_configDirectory << "': " << exception.what() << '\n');
        return false;
    }

    nlohmann::json json;
    json["version"] = 4;
    json["preferred_ide"] = m_preferredIdeId;
    json["show_asset_thumbnails"] = m_showAssetThumbnails;
    json["detailed_render_profiling"] = m_detailedRenderProfilingEnabled;
    json["editor_camera"] = {
        {"move_speed", m_editorCameraSettings.moveSpeed},
        {"mouse_sensitivity", m_editorCameraSettings.mouseSensitivity},
        {"projection_mode", m_editorCameraSettings.projectionMode},
        {"near_plane", m_editorCameraSettings.nearPlane},
        {"far_plane", m_editorCameraSettings.farPlane},
        {"fov", m_editorCameraSettings.fov},
        {"orthographic_size", m_editorCameraSettings.orthographicSize}};

    const auto &renderSettings = RenderQualitySettings::getInstance();
    json["render_settings"] = {
        {"shadow_quality", renderSettings.getShadowResolution()},
        {"shadow_cascade_count", renderSettings.getShadowCascadeCount()},
        {"shadow_max_distance", renderSettings.shadowMaxDistance},
        {"anti_aliasing_mode", static_cast<int>(renderSettings.getAntiAliasingMode())},
        {"enable_vsync", renderSettings.enableVSync},
        {"enable_post_processing", renderSettings.enablePostProcessing},
        {"enable_fxaa", renderSettings.enableFXAA},
        {"enable_smaa", renderSettings.enableSMAA},
        {"enable_taa", renderSettings.enableTAA},
        {"enable_cmaa", renderSettings.enableCMAA},
        {"taa_history_weight", renderSettings.taaHistoryWeight},
        {"msaa_mode", static_cast<int>(renderSettings.msaaMode)},
        {"anisotropy_mode", static_cast<int>(renderSettings.anisotropyMode)},
        {"enable_texture_mipmaps", renderSettings.enableTextureMipmaps},
        {"texture_mip_level_limit", renderSettings.textureMipLevelLimit},
        {"texture_lod_bias", renderSettings.textureLodBias},
        {"texture_lod_distance_start", renderSettings.textureLodDistanceStart},
        {"texture_lod_distance_end", renderSettings.textureLodDistanceEnd},
        {"texture_lod_distance_bias", renderSettings.textureLodDistanceBias},
        {"texture_preview_max_dimension", renderSettings.texturePreviewMaxDimension},
        {"texture_import_max_dimension", renderSettings.textureImportMaxDimension},
        {"enable_texture_oom_fallback", renderSettings.enableTextureOomFallback},
        {"texture_oom_fallback_dimension", renderSettings.textureOomFallbackDimension},
        {"render_scale", renderSettings.renderScale},
        {"enable_ssao", renderSettings.enableSSAO},
        {"ssao_radius", renderSettings.ssaoRadius},
        {"ssao_bias", renderSettings.ssaoBias},
        {"ssao_strength", renderSettings.ssaoStrength},
        {"ssao_samples", renderSettings.ssaoSamples},
        {"enable_gtao", renderSettings.enableGTAO},
        {"gtao_directions", renderSettings.gtaoDirections},
        {"gtao_steps", renderSettings.gtaoSteps},
        {"use_bent_normals", renderSettings.useBentNormals},
        {"enable_anisotropy", renderSettings.enableAnisotropy},
        {"anisotropy_strength", renderSettings.anisotropyStrength},
        {"anisotropy_rotation", renderSettings.anisotropyRotation},
        {"shadow_ambient_strength", renderSettings.shadowAmbientStrength},
        {"enable_shadow_occlusion_culling", renderSettings.enableShadowOcclusionCulling},
        {"enable_occlusion_culling", renderSettings.enableOcclusionCulling},
        {"occlusion_probe_interval", renderSettings.occlusionProbeInterval},
        {"occlusion_visible_requery_interval", renderSettings.occlusionVisibleRequeryInterval},
        {"occlusion_occluded_confirmation_queries", renderSettings.occlusionOccludedConfirmationQueries},
        {"occlusion_max_instances_per_batch", renderSettings.occlusionMaxInstancesPerBatch},
        {"occlusion_fast_motion_probe_interval", renderSettings.occlusionFastMotionProbeInterval},
        {"occlusion_fast_motion_visible_requery_interval", renderSettings.occlusionFastMotionVisibleRequeryInterval},
        {"occlusion_fast_motion_stale_reveal_frames", renderSettings.occlusionFastMotionStaleRevealFrames},
        {"occlusion_fast_motion_translation_threshold", renderSettings.occlusionFastMotionTranslationThreshold},
        {"occlusion_fast_motion_forward_dot_threshold", renderSettings.occlusionFastMotionForwardDotThreshold},
        {"shadow_occlusion_visibility_grace_frames", renderSettings.shadowOcclusionVisibilityGraceFrames},
        {"enable_bloom", renderSettings.enableBloom},
        {"bloom_threshold", renderSettings.bloomThreshold},
        {"bloom_knee", renderSettings.bloomKnee},
        {"bloom_strength", renderSettings.bloomStrength},
        {"enable_ssr", renderSettings.enableSSR},
        {"ssr_max_distance", renderSettings.ssrMaxDistance},
        {"ssr_thickness", renderSettings.ssrThickness},
        {"ssr_strength", renderSettings.ssrStrength},
        {"ssr_steps", renderSettings.ssrSteps},
        {"enable_contact_shadows", renderSettings.enableContactShadows},
        {"contact_shadow_length", renderSettings.contactShadowLength},
        {"contact_shadow_strength", renderSettings.contactShadowStrength},
        {"contact_shadow_steps", renderSettings.contactShadowSteps},
        {"enable_lut_grading", renderSettings.enableLUTGrading},
        {"lut_grading_path", renderSettings.lutGradingPath},
        {"lut_grading_strength", renderSettings.lutGradingStrength},
        {"enable_color_grading", renderSettings.enableColorGrading},
        {"color_grading_saturation", renderSettings.colorGradingSaturation},
        {"color_grading_contrast", renderSettings.colorGradingContrast},
        {"color_grading_temperature", renderSettings.colorGradingTemperature},
        {"color_grading_tint", renderSettings.colorGradingTint},
        {"enable_vignette", renderSettings.enableVignette},
        {"vignette_strength", renderSettings.vignetteStrength},
        {"enable_film_grain", renderSettings.enableFilmGrain},
        {"film_grain_strength", renderSettings.filmGrainStrength},
        {"enable_chromatic_aberration", renderSettings.enableChromaticAberration},
        {"chromatic_aberration_strength", renderSettings.chromaticAberrationStrength},
        {"enable_ibl", renderSettings.enableIBL},
        {"ibl_diffuse_intensity", renderSettings.iblDiffuseIntensity},
        {"ibl_specular_intensity", renderSettings.iblSpecularIntensity}};

    std::ofstream file(m_configFilePath);
    if (!file.is_open())
    {
        VX_ENGINE_ERROR_STREAM("Failed to save engine config to '" << m_configFilePath << "'\n");
        return false;
    }

    file << std::setw(4) << json << '\n';
    return file.good();
}

const std::filesystem::path &EngineConfig::getConfigDirectory() const
{
    return m_configDirectory;
}

const std::filesystem::path &EngineConfig::getConfigFilePath() const
{
    return m_configFilePath;
}

const std::vector<EngineConfig::IdeInfo> &EngineConfig::getDetectedIdes() const
{
    return m_detectedIdes;
}

std::optional<EngineConfig::IdeInfo> EngineConfig::findIde(const std::string &ideId) const
{
    auto ideIterator = std::find_if(m_detectedIdes.begin(), m_detectedIdes.end(), [&](const IdeInfo &ide)
                                    { return ide.id == ideId; });

    if (ideIterator == m_detectedIdes.end())
        return std::nullopt;

    return *ideIterator;
}

const std::string &EngineConfig::getPreferredIdeId() const
{
    return m_preferredIdeId;
}

void EngineConfig::setPreferredIdeId(const std::string &ideId)
{
    if (!isKnownIdeId(ideId))
        return;

    m_preferredIdeId = ideId;
}

bool EngineConfig::getShowAssetThumbnails() const
{
    return m_showAssetThumbnails;
}

void EngineConfig::setShowAssetThumbnails(bool enabled)
{
    m_showAssetThumbnails = enabled;
}

bool EngineConfig::getDetailedRenderProfilingEnabled() const
{
    return m_detailedRenderProfilingEnabled;
}

void EngineConfig::setDetailedRenderProfilingEnabled(bool enabled)
{
    m_detailedRenderProfilingEnabled = enabled;
}

const EngineConfig::EditorCameraSettings &EngineConfig::getEditorCameraSettings() const
{
    return m_editorCameraSettings;
}

void EngineConfig::setEditorCameraSettings(const EditorCameraSettings &settings)
{
    m_editorCameraSettings = settings;
    m_editorCameraSettings.moveSpeed = std::max(0.05f, m_editorCameraSettings.moveSpeed);
    m_editorCameraSettings.mouseSensitivity = std::max(0.005f, m_editorCameraSettings.mouseSensitivity);
    m_editorCameraSettings.projectionMode = static_cast<uint8_t>(std::clamp(static_cast<int>(m_editorCameraSettings.projectionMode), 0, 1));
    m_editorCameraSettings.nearPlane = std::max(0.001f, m_editorCameraSettings.nearPlane);
    m_editorCameraSettings.farPlane = std::max(m_editorCameraSettings.nearPlane + 0.001f, m_editorCameraSettings.farPlane);
    m_editorCameraSettings.fov = std::clamp(m_editorCameraSettings.fov, 1.0f, 179.0f);
    m_editorCameraSettings.orthographicSize = std::max(0.01f, m_editorCameraSettings.orthographicSize);
}

std::optional<EngineConfig::IdeInfo> EngineConfig::findPreferredVSCodeIde() const
{
    if (isVSCodeIdeId(m_preferredIdeId))
    {
        if (auto ide = findIde(m_preferredIdeId))
            return ide;
    }

    static const std::array<std::string, 3> vscodeIdePriority = {
        "vscode",
        "vscode_insiders",
        "vscodium"};

    for (const auto &ideId : vscodeIdePriority)
    {
        if (auto ide = findIde(ideId))
            return ide;
    }

    return std::nullopt;
}

bool EngineConfig::hasVSCodeIde() const
{
    return findPreferredVSCodeIde().has_value();
}

std::filesystem::path EngineConfig::resolveConfigDirectory()
{
#if defined(_WIN32)
    if (const char *appData = std::getenv("APPDATA"))
        if (*appData != '\0')
            return std::filesystem::path(appData) / "Velix";

    if (const char *localAppData = std::getenv("LOCALAPPDATA"))
        if (*localAppData != '\0')
            return std::filesystem::path(localAppData) / "Velix";

    if (const char *userProfile = std::getenv("USERPROFILE"))
        if (*userProfile != '\0')
            return std::filesystem::path(userProfile) / "AppData" / "Roaming" / "Velix";

    return std::filesystem::current_path() / ".velix";
#else
    if (const char *xdgConfigHome = std::getenv("XDG_CONFIG_HOME"))
        if (*xdgConfigHome != '\0')
            return std::filesystem::path(xdgConfigHome) / "Velix";

    if (const char *home = std::getenv("HOME"))
        if (*home != '\0')
            return std::filesystem::path(home) / ".config" / "Velix";

    return std::filesystem::current_path() / ".config" / "Velix";
#endif
}

void EngineConfig::detectInstalledIdes()
{
    m_detectedIdes.clear();

    std::unordered_set<std::string> addedIdeIds;
    for (const auto &ideDefinition : getIdeDefinitions())
    {
        for (const auto &command : ideDefinition.commands)
        {
            auto executablePath = findExecutableInPath(command);
            if (!executablePath.has_value())
                continue;

            if (!addedIdeIds.insert(ideDefinition.id).second)
                break;

            IdeInfo ideInfo;
            ideInfo.id = ideDefinition.id;
            ideInfo.displayName = ideDefinition.displayName;
            ideInfo.command = executablePath->string();
            m_detectedIdes.emplace_back(std::move(ideInfo));
            break;
        }
    }
}

void EngineConfig::applyDefaults()
{
    m_configDirectory = resolveConfigDirectory();
    m_configFilePath = m_configDirectory / "engine_config.json";
    m_preferredIdeId = "vscode";
    m_showAssetThumbnails = true;
    m_detailedRenderProfilingEnabled = true;
    m_editorCameraSettings = EditorCameraSettings{};
}

bool EngineConfig::loadFromDisk()
{
    if (m_configFilePath.empty() || !std::filesystem::exists(m_configFilePath))
        return true;

    std::ifstream file(m_configFilePath);
    if (!file.is_open())
    {
        VX_ENGINE_WARNING_STREAM("Failed to open engine config file: " << m_configFilePath << '\n');
        return false;
    }

    nlohmann::json json;

    try
    {
        file >> json;
    }
    catch (const nlohmann::json::parse_error &exception)
    {
        VX_ENGINE_WARNING_STREAM("Failed to parse engine config file '" << m_configFilePath << "': " << exception.what() << '\n');
        return false;
    }

    if (json.contains("preferred_ide") && json["preferred_ide"].is_string())
        m_preferredIdeId = json["preferred_ide"].get<std::string>();

    if (json.contains("show_asset_thumbnails") && json["show_asset_thumbnails"].is_boolean())
        m_showAssetThumbnails = json["show_asset_thumbnails"].get<bool>();

    if (json.contains("detailed_render_profiling") && json["detailed_render_profiling"].is_boolean())
        m_detailedRenderProfilingEnabled = json["detailed_render_profiling"].get<bool>();

    if (json.contains("editor_camera") && json["editor_camera"].is_object())
    {
        const auto &cameraJson = json["editor_camera"];

        if (cameraJson.contains("move_speed") && cameraJson["move_speed"].is_number())
            m_editorCameraSettings.moveSpeed = cameraJson["move_speed"].get<float>();

        if (cameraJson.contains("mouse_sensitivity") && cameraJson["mouse_sensitivity"].is_number())
            m_editorCameraSettings.mouseSensitivity = cameraJson["mouse_sensitivity"].get<float>();

        if (cameraJson.contains("projection_mode") && cameraJson["projection_mode"].is_number_integer())
            m_editorCameraSettings.projectionMode = static_cast<uint8_t>(std::clamp(cameraJson["projection_mode"].get<int>(), 0, 1));

        if (cameraJson.contains("near_plane") && cameraJson["near_plane"].is_number())
            m_editorCameraSettings.nearPlane = cameraJson["near_plane"].get<float>();

        if (cameraJson.contains("far_plane") && cameraJson["far_plane"].is_number())
            m_editorCameraSettings.farPlane = cameraJson["far_plane"].get<float>();

        if (cameraJson.contains("fov") && cameraJson["fov"].is_number())
            m_editorCameraSettings.fov = cameraJson["fov"].get<float>();

        if (cameraJson.contains("orthographic_size") && cameraJson["orthographic_size"].is_number())
            m_editorCameraSettings.orthographicSize = cameraJson["orthographic_size"].get<float>();

        setEditorCameraSettings(m_editorCameraSettings);
    }

    if (json.contains("render_settings") && json["render_settings"].is_object())
    {
        auto &settings = RenderQualitySettings::getInstance();
        const auto &renderSettingsJson = json["render_settings"];

        if (renderSettingsJson.contains("shadow_quality") && renderSettingsJson["shadow_quality"].is_number_unsigned())
        {
            const uint32_t shadowQuality = renderSettingsJson["shadow_quality"].get<uint32_t>();
            switch (shadowQuality)
            {
            case static_cast<uint32_t>(RenderQualitySettings::ShadowQuality::Low):
                settings.shadowQuality = RenderQualitySettings::ShadowQuality::Low;
                break;
            case static_cast<uint32_t>(RenderQualitySettings::ShadowQuality::Medium):
                settings.shadowQuality = RenderQualitySettings::ShadowQuality::Medium;
                break;
            case static_cast<uint32_t>(RenderQualitySettings::ShadowQuality::High):
                settings.shadowQuality = RenderQualitySettings::ShadowQuality::High;
                break;
            case static_cast<uint32_t>(RenderQualitySettings::ShadowQuality::Ultra):
                settings.shadowQuality = RenderQualitySettings::ShadowQuality::Ultra;
                break;
            default:
                break;
            }
        }

        if (renderSettingsJson.contains("shadow_cascade_count") && renderSettingsJson["shadow_cascade_count"].is_number_unsigned())
        {
            const uint32_t cascadeCount = renderSettingsJson["shadow_cascade_count"].get<uint32_t>();
            switch (cascadeCount)
            {
            case 1u:
                settings.shadowCascadeCount = RenderQualitySettings::ShadowCascadeCount::X1;
                break;
            case 2u:
                settings.shadowCascadeCount = RenderQualitySettings::ShadowCascadeCount::X2;
                break;
            case 4u:
                settings.shadowCascadeCount = RenderQualitySettings::ShadowCascadeCount::X4;
                break;
            default:
                break;
            }
        }

        if (renderSettingsJson.contains("shadow_max_distance") && renderSettingsJson["shadow_max_distance"].is_number())
            settings.shadowMaxDistance = std::max(renderSettingsJson["shadow_max_distance"].get<float>(), 20.0f);

        if (renderSettingsJson.contains("anti_aliasing_mode") && renderSettingsJson["anti_aliasing_mode"].is_number_integer())
        {
            const int mode = std::clamp(renderSettingsJson["anti_aliasing_mode"].get<int>(), 0, 4);
            settings.setAntiAliasingMode(static_cast<RenderQualitySettings::AntiAliasingMode>(mode));
        }

        if (renderSettingsJson.contains("enable_fxaa") && renderSettingsJson["enable_fxaa"].is_boolean())
            settings.enableFXAA = renderSettingsJson["enable_fxaa"].get<bool>();
        if (renderSettingsJson.contains("enable_smaa") && renderSettingsJson["enable_smaa"].is_boolean())
            settings.enableSMAA = renderSettingsJson["enable_smaa"].get<bool>();
        if (renderSettingsJson.contains("enable_taa") && renderSettingsJson["enable_taa"].is_boolean())
            settings.enableTAA = renderSettingsJson["enable_taa"].get<bool>();
        if (renderSettingsJson.contains("enable_cmaa") && renderSettingsJson["enable_cmaa"].is_boolean())
            settings.enableCMAA = renderSettingsJson["enable_cmaa"].get<bool>();
        if (renderSettingsJson.contains("taa_history_weight") && renderSettingsJson["taa_history_weight"].is_number())
            settings.taaHistoryWeight = std::clamp(renderSettingsJson["taa_history_weight"].get<float>(), 0.0f, 1.0f);

        if (renderSettingsJson.contains("msaa_mode") && renderSettingsJson["msaa_mode"].is_number_integer())
        {
            const int mode = std::clamp(renderSettingsJson["msaa_mode"].get<int>(), 0, 4);
            settings.msaaMode = static_cast<RenderQualitySettings::MSAAMode>(mode);
        }

        if (renderSettingsJson.contains("anisotropy_mode") && renderSettingsJson["anisotropy_mode"].is_number_integer())
        {
            const int mode = std::clamp(renderSettingsJson["anisotropy_mode"].get<int>(), 0, 4);
            settings.anisotropyMode = static_cast<RenderQualitySettings::AnisotropyMode>(mode);
        }

        if (renderSettingsJson.contains("enable_texture_mipmaps") && renderSettingsJson["enable_texture_mipmaps"].is_boolean())
            settings.enableTextureMipmaps = renderSettingsJson["enable_texture_mipmaps"].get<bool>();
        if (renderSettingsJson.contains("texture_mip_level_limit") && renderSettingsJson["texture_mip_level_limit"].is_number_integer())
            settings.textureMipLevelLimit = std::clamp(renderSettingsJson["texture_mip_level_limit"].get<int>(), 0, 16);
        if (renderSettingsJson.contains("texture_lod_bias") && renderSettingsJson["texture_lod_bias"].is_number())
            settings.textureLodBias = std::clamp(renderSettingsJson["texture_lod_bias"].get<float>(), -2.0f, 8.0f);
        if (renderSettingsJson.contains("texture_lod_distance_start") && renderSettingsJson["texture_lod_distance_start"].is_number())
            settings.textureLodDistanceStart = std::clamp(renderSettingsJson["texture_lod_distance_start"].get<float>(), 0.0f, 10000.0f);
        if (renderSettingsJson.contains("texture_lod_distance_end") && renderSettingsJson["texture_lod_distance_end"].is_number())
            settings.textureLodDistanceEnd = std::clamp(renderSettingsJson["texture_lod_distance_end"].get<float>(), 0.0f, 10000.0f);
        if (renderSettingsJson.contains("texture_lod_distance_bias") && renderSettingsJson["texture_lod_distance_bias"].is_number())
            settings.textureLodDistanceBias = std::clamp(renderSettingsJson["texture_lod_distance_bias"].get<float>(), 0.0f, 8.0f);
        if (renderSettingsJson.contains("texture_preview_max_dimension") && renderSettingsJson["texture_preview_max_dimension"].is_number_unsigned())
            settings.texturePreviewMaxDimension = std::clamp(renderSettingsJson["texture_preview_max_dimension"].get<uint32_t>(), 64u, 2048u);
        if (renderSettingsJson.contains("texture_import_max_dimension") && renderSettingsJson["texture_import_max_dimension"].is_number_unsigned())
        {
            settings.textureImportMaxDimension = std::clamp(renderSettingsJson["texture_import_max_dimension"].get<uint32_t>(), 0u, 16384u);
            if (settings.textureImportMaxDimension != 0u && settings.textureImportMaxDimension < 64u)
                settings.textureImportMaxDimension = 64u;
        }
        if (renderSettingsJson.contains("enable_texture_oom_fallback") && renderSettingsJson["enable_texture_oom_fallback"].is_boolean())
            settings.enableTextureOomFallback = renderSettingsJson["enable_texture_oom_fallback"].get<bool>();
        if (renderSettingsJson.contains("texture_oom_fallback_dimension") && renderSettingsJson["texture_oom_fallback_dimension"].is_number_unsigned())
            settings.textureOomFallbackDimension = std::clamp(renderSettingsJson["texture_oom_fallback_dimension"].get<uint32_t>(), 4u, 64u);

        if (settings.textureLodDistanceEnd < settings.textureLodDistanceStart)
            settings.textureLodDistanceEnd = settings.textureLodDistanceStart;

        AssetsLoader::setTextureImportMaxDimension(settings.textureImportMaxDimension);

        if (renderSettingsJson.contains("enable_post_processing") && renderSettingsJson["enable_post_processing"].is_boolean())
            settings.enablePostProcessing = renderSettingsJson["enable_post_processing"].get<bool>();
        if (renderSettingsJson.contains("enable_vsync") && renderSettingsJson["enable_vsync"].is_boolean())
            settings.enableVSync = renderSettingsJson["enable_vsync"].get<bool>();
        if (renderSettingsJson.contains("render_scale") && renderSettingsJson["render_scale"].is_number())
            settings.renderScale = std::clamp(renderSettingsJson["render_scale"].get<float>(), 0.25f, 2.0f);

        if (renderSettingsJson.contains("enable_ssao") && renderSettingsJson["enable_ssao"].is_boolean())
            settings.enableSSAO = renderSettingsJson["enable_ssao"].get<bool>();
        if (renderSettingsJson.contains("ssao_radius") && renderSettingsJson["ssao_radius"].is_number())
            settings.ssaoRadius = renderSettingsJson["ssao_radius"].get<float>();
        if (renderSettingsJson.contains("ssao_bias") && renderSettingsJson["ssao_bias"].is_number())
            settings.ssaoBias = renderSettingsJson["ssao_bias"].get<float>();
        if (renderSettingsJson.contains("ssao_strength") && renderSettingsJson["ssao_strength"].is_number())
            settings.ssaoStrength = renderSettingsJson["ssao_strength"].get<float>();
        if (renderSettingsJson.contains("ssao_samples") && renderSettingsJson["ssao_samples"].is_number_integer())
            settings.ssaoSamples = std::clamp(renderSettingsJson["ssao_samples"].get<int>(), 4, 64);
        if (renderSettingsJson.contains("enable_gtao") && renderSettingsJson["enable_gtao"].is_boolean())
            settings.enableGTAO = renderSettingsJson["enable_gtao"].get<bool>();
        if (renderSettingsJson.contains("gtao_directions") && renderSettingsJson["gtao_directions"].is_number_integer())
            settings.gtaoDirections = std::clamp(renderSettingsJson["gtao_directions"].get<int>(), 2, 8);
        if (renderSettingsJson.contains("gtao_steps") && renderSettingsJson["gtao_steps"].is_number_integer())
            settings.gtaoSteps = std::clamp(renderSettingsJson["gtao_steps"].get<int>(), 2, 8);
        if (renderSettingsJson.contains("use_bent_normals") && renderSettingsJson["use_bent_normals"].is_boolean())
            settings.useBentNormals = renderSettingsJson["use_bent_normals"].get<bool>();

        if (renderSettingsJson.contains("enable_anisotropy") && renderSettingsJson["enable_anisotropy"].is_boolean())
            settings.enableAnisotropy = renderSettingsJson["enable_anisotropy"].get<bool>();
        if (renderSettingsJson.contains("anisotropy_strength") && renderSettingsJson["anisotropy_strength"].is_number())
            settings.anisotropyStrength = std::clamp(renderSettingsJson["anisotropy_strength"].get<float>(), -1.0f, 1.0f);
        if (renderSettingsJson.contains("anisotropy_rotation") && renderSettingsJson["anisotropy_rotation"].is_number())
            settings.anisotropyRotation = renderSettingsJson["anisotropy_rotation"].get<float>();

        if (renderSettingsJson.contains("shadow_ambient_strength") && renderSettingsJson["shadow_ambient_strength"].is_number())
            settings.shadowAmbientStrength = std::clamp(renderSettingsJson["shadow_ambient_strength"].get<float>(), 0.0f, 1.0f);
        if (renderSettingsJson.contains("enable_shadow_occlusion_culling") && renderSettingsJson["enable_shadow_occlusion_culling"].is_boolean())
            settings.enableShadowOcclusionCulling = renderSettingsJson["enable_shadow_occlusion_culling"].get<bool>();

        if (renderSettingsJson.contains("enable_occlusion_culling") && renderSettingsJson["enable_occlusion_culling"].is_boolean())
            settings.enableOcclusionCulling = renderSettingsJson["enable_occlusion_culling"].get<bool>();
        if (renderSettingsJson.contains("occlusion_probe_interval") && renderSettingsJson["occlusion_probe_interval"].is_number_integer())
            settings.occlusionProbeInterval = std::clamp(renderSettingsJson["occlusion_probe_interval"].get<int>(), 1, 240);
        if (renderSettingsJson.contains("occlusion_visible_requery_interval") && renderSettingsJson["occlusion_visible_requery_interval"].is_number_integer())
            settings.occlusionVisibleRequeryInterval = std::clamp(renderSettingsJson["occlusion_visible_requery_interval"].get<int>(), 1, 480);
        if (renderSettingsJson.contains("occlusion_occluded_confirmation_queries") && renderSettingsJson["occlusion_occluded_confirmation_queries"].is_number_integer())
            settings.occlusionOccludedConfirmationQueries = std::clamp(renderSettingsJson["occlusion_occluded_confirmation_queries"].get<int>(), 1, 8);
        if (renderSettingsJson.contains("occlusion_max_instances_per_batch") && renderSettingsJson["occlusion_max_instances_per_batch"].is_number_integer())
            settings.occlusionMaxInstancesPerBatch = std::clamp(renderSettingsJson["occlusion_max_instances_per_batch"].get<int>(), 1, 1024);
        if (renderSettingsJson.contains("occlusion_fast_motion_probe_interval") && renderSettingsJson["occlusion_fast_motion_probe_interval"].is_number_integer())
            settings.occlusionFastMotionProbeInterval = std::clamp(renderSettingsJson["occlusion_fast_motion_probe_interval"].get<int>(), 1, 240);
        if (renderSettingsJson.contains("occlusion_fast_motion_visible_requery_interval") && renderSettingsJson["occlusion_fast_motion_visible_requery_interval"].is_number_integer())
            settings.occlusionFastMotionVisibleRequeryInterval = std::clamp(renderSettingsJson["occlusion_fast_motion_visible_requery_interval"].get<int>(), 1, 240);
        if (renderSettingsJson.contains("occlusion_fast_motion_stale_reveal_frames") && renderSettingsJson["occlusion_fast_motion_stale_reveal_frames"].is_number_integer())
            settings.occlusionFastMotionStaleRevealFrames = std::clamp(renderSettingsJson["occlusion_fast_motion_stale_reveal_frames"].get<int>(), 0, 240);
        if (renderSettingsJson.contains("occlusion_fast_motion_translation_threshold") && renderSettingsJson["occlusion_fast_motion_translation_threshold"].is_number())
            settings.occlusionFastMotionTranslationThreshold = std::clamp(renderSettingsJson["occlusion_fast_motion_translation_threshold"].get<float>(), 0.0f, 10.0f);
        if (renderSettingsJson.contains("occlusion_fast_motion_forward_dot_threshold") && renderSettingsJson["occlusion_fast_motion_forward_dot_threshold"].is_number())
            settings.occlusionFastMotionForwardDotThreshold = std::clamp(renderSettingsJson["occlusion_fast_motion_forward_dot_threshold"].get<float>(), -1.0f, 1.0f);
        if (renderSettingsJson.contains("shadow_occlusion_visibility_grace_frames") && renderSettingsJson["shadow_occlusion_visibility_grace_frames"].is_number_integer())
            settings.shadowOcclusionVisibilityGraceFrames = std::clamp(renderSettingsJson["shadow_occlusion_visibility_grace_frames"].get<int>(), 0, 600);

        if (renderSettingsJson.contains("enable_bloom") && renderSettingsJson["enable_bloom"].is_boolean())
            settings.enableBloom = renderSettingsJson["enable_bloom"].get<bool>();
        if (renderSettingsJson.contains("bloom_threshold") && renderSettingsJson["bloom_threshold"].is_number())
            settings.bloomThreshold = std::clamp(renderSettingsJson["bloom_threshold"].get<float>(), 0.0f, 5.0f);
        if (renderSettingsJson.contains("bloom_knee") && renderSettingsJson["bloom_knee"].is_number())
            settings.bloomKnee = std::clamp(renderSettingsJson["bloom_knee"].get<float>(), 0.0f, 1.0f);
        if (renderSettingsJson.contains("bloom_strength") && renderSettingsJson["bloom_strength"].is_number())
            settings.bloomStrength = std::clamp(renderSettingsJson["bloom_strength"].get<float>(), 0.0f, 2.0f);

        if (renderSettingsJson.contains("enable_ssr") && renderSettingsJson["enable_ssr"].is_boolean())
            settings.enableSSR = renderSettingsJson["enable_ssr"].get<bool>();
        if (renderSettingsJson.contains("ssr_max_distance") && renderSettingsJson["ssr_max_distance"].is_number())
            settings.ssrMaxDistance = std::clamp(renderSettingsJson["ssr_max_distance"].get<float>(), 1.0f, 200.0f);
        if (renderSettingsJson.contains("ssr_thickness") && renderSettingsJson["ssr_thickness"].is_number())
            settings.ssrThickness = std::clamp(renderSettingsJson["ssr_thickness"].get<float>(), 0.0f, 2.0f);
        if (renderSettingsJson.contains("ssr_strength") && renderSettingsJson["ssr_strength"].is_number())
            settings.ssrStrength = std::clamp(renderSettingsJson["ssr_strength"].get<float>(), 0.0f, 1.0f);
        if (renderSettingsJson.contains("ssr_steps") && renderSettingsJson["ssr_steps"].is_number_integer())
            settings.ssrSteps = std::clamp(renderSettingsJson["ssr_steps"].get<int>(), 4, 64);

        if (renderSettingsJson.contains("enable_contact_shadows") && renderSettingsJson["enable_contact_shadows"].is_boolean())
            settings.enableContactShadows = renderSettingsJson["enable_contact_shadows"].get<bool>();
        if (renderSettingsJson.contains("contact_shadow_length") && renderSettingsJson["contact_shadow_length"].is_number())
            settings.contactShadowLength = std::clamp(renderSettingsJson["contact_shadow_length"].get<float>(), 0.1f, 5.0f);
        if (renderSettingsJson.contains("contact_shadow_strength") && renderSettingsJson["contact_shadow_strength"].is_number())
            settings.contactShadowStrength = std::clamp(renderSettingsJson["contact_shadow_strength"].get<float>(), 0.0f, 1.0f);
        if (renderSettingsJson.contains("contact_shadow_steps") && renderSettingsJson["contact_shadow_steps"].is_number_integer())
            settings.contactShadowSteps = std::clamp(renderSettingsJson["contact_shadow_steps"].get<int>(), 4, 32);

        if (renderSettingsJson.contains("enable_lut_grading") && renderSettingsJson["enable_lut_grading"].is_boolean())
            settings.enableLUTGrading = renderSettingsJson["enable_lut_grading"].get<bool>();
        if (renderSettingsJson.contains("lut_grading_path") && renderSettingsJson["lut_grading_path"].is_string())
            settings.lutGradingPath = renderSettingsJson["lut_grading_path"].get<std::string>();
        if (renderSettingsJson.contains("lut_grading_strength") && renderSettingsJson["lut_grading_strength"].is_number())
            settings.lutGradingStrength = std::clamp(renderSettingsJson["lut_grading_strength"].get<float>(), 0.0f, 1.0f);

        if (renderSettingsJson.contains("enable_color_grading") && renderSettingsJson["enable_color_grading"].is_boolean())
            settings.enableColorGrading = renderSettingsJson["enable_color_grading"].get<bool>();
        if (renderSettingsJson.contains("color_grading_saturation") && renderSettingsJson["color_grading_saturation"].is_number())
            settings.colorGradingSaturation = std::clamp(renderSettingsJson["color_grading_saturation"].get<float>(), 0.0f, 2.0f);
        if (renderSettingsJson.contains("color_grading_contrast") && renderSettingsJson["color_grading_contrast"].is_number())
            settings.colorGradingContrast = std::clamp(renderSettingsJson["color_grading_contrast"].get<float>(), 0.0f, 2.0f);
        if (renderSettingsJson.contains("color_grading_temperature") && renderSettingsJson["color_grading_temperature"].is_number())
            settings.colorGradingTemperature = std::clamp(renderSettingsJson["color_grading_temperature"].get<float>(), -1.0f, 1.0f);
        if (renderSettingsJson.contains("color_grading_tint") && renderSettingsJson["color_grading_tint"].is_number())
            settings.colorGradingTint = std::clamp(renderSettingsJson["color_grading_tint"].get<float>(), -1.0f, 1.0f);

        if (renderSettingsJson.contains("enable_vignette") && renderSettingsJson["enable_vignette"].is_boolean())
            settings.enableVignette = renderSettingsJson["enable_vignette"].get<bool>();
        if (renderSettingsJson.contains("vignette_strength") && renderSettingsJson["vignette_strength"].is_number())
            settings.vignetteStrength = std::clamp(renderSettingsJson["vignette_strength"].get<float>(), 0.0f, 1.0f);
        if (renderSettingsJson.contains("enable_film_grain") && renderSettingsJson["enable_film_grain"].is_boolean())
            settings.enableFilmGrain = renderSettingsJson["enable_film_grain"].get<bool>();
        if (renderSettingsJson.contains("film_grain_strength") && renderSettingsJson["film_grain_strength"].is_number())
            settings.filmGrainStrength = std::clamp(renderSettingsJson["film_grain_strength"].get<float>(), 0.0f, 0.2f);
        if (renderSettingsJson.contains("enable_chromatic_aberration") && renderSettingsJson["enable_chromatic_aberration"].is_boolean())
            settings.enableChromaticAberration = renderSettingsJson["enable_chromatic_aberration"].get<bool>();
        if (renderSettingsJson.contains("chromatic_aberration_strength") && renderSettingsJson["chromatic_aberration_strength"].is_number())
            settings.chromaticAberrationStrength = std::clamp(renderSettingsJson["chromatic_aberration_strength"].get<float>(), 0.0f, 0.02f);

        if (renderSettingsJson.contains("enable_ibl") && renderSettingsJson["enable_ibl"].is_boolean())
            settings.enableIBL = renderSettingsJson["enable_ibl"].get<bool>();
        if (renderSettingsJson.contains("ibl_diffuse_intensity") && renderSettingsJson["ibl_diffuse_intensity"].is_number())
            settings.iblDiffuseIntensity = std::clamp(renderSettingsJson["ibl_diffuse_intensity"].get<float>(), 0.0f, 3.0f);
        if (renderSettingsJson.contains("ibl_specular_intensity") && renderSettingsJson["ibl_specular_intensity"].is_number())
            settings.iblSpecularIntensity = std::clamp(renderSettingsJson["ibl_specular_intensity"].get<float>(), 0.0f, 3.0f);
    }

    return true;
}

bool EngineConfig::isKnownIdeId(const std::string &ideId) const
{
    const auto &definitions = getIdeDefinitions();
    return std::any_of(definitions.begin(), definitions.end(), [&](const IdeDefinition &definition)
                       { return definition.id == ideId; });
}

ELIX_NESTED_NAMESPACE_END
