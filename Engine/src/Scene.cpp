#include "Engine/Scene.hpp"

#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Components/CameraComponent.hpp"
#include "Engine/Components/RigidBodyComponent.hpp"
#include "Engine/Components/CollisionComponent.hpp"
#include "Engine/Components/CharacterMovementComponent.hpp"
#include "Engine/Components/AudioComponent.hpp"
#include "Engine/Components/ScriptComponent.hpp"
#include "Engine/Components/ParticleSystemComponent.hpp"

#include "Engine/Particles/Modules/SpawnModule.hpp"
#include "Engine/Particles/Modules/LifetimeModule.hpp"
#include "Engine/Particles/Modules/InitialVelocityModule.hpp"
#include "Engine/Particles/Modules/ColorOverLifetimeModule.hpp"
#include "Engine/Particles/Modules/SizeOverLifetimeModule.hpp"
#include "Engine/Particles/Modules/ForceModule.hpp"
#include "Engine/Particles/Modules/RendererModule.hpp"

#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Scripting/ScriptsRegister.hpp"

#include "Engine/Mesh.hpp"
#include "Engine/Primitives.hpp"

#include "nlohmann/json.hpp"
#include <glm/common.hpp>
#include <glm/gtx/quaternion.hpp>

#include <fstream>

#include <iostream>

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <type_traits>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace
{
    const char *scriptVariableTypeToString(elix::engine::Script::ExposedVariableType type)
    {
        using ExposedVariableType = elix::engine::Script::ExposedVariableType;
        switch (type)
        {
        case ExposedVariableType::Bool:
            return "bool";
        case ExposedVariableType::Int:
            return "int";
        case ExposedVariableType::Float:
            return "float";
        case ExposedVariableType::String:
            return "string";
        case ExposedVariableType::Vec2:
            return "vec2";
        case ExposedVariableType::Vec3:
            return "vec3";
        case ExposedVariableType::Vec4:
            return "vec4";
        case ExposedVariableType::Entity:
            return "entity";
        default:
            return "float";
        }
    }

    bool scriptVariableTypeFromString(const std::string &type, elix::engine::Script::ExposedVariableType &outType)
    {
        using ExposedVariableType = elix::engine::Script::ExposedVariableType;
        if (type == "bool")
        {
            outType = ExposedVariableType::Bool;
            return true;
        }
        if (type == "int")
        {
            outType = ExposedVariableType::Int;
            return true;
        }
        if (type == "float")
        {
            outType = ExposedVariableType::Float;
            return true;
        }
        if (type == "string")
        {
            outType = ExposedVariableType::String;
            return true;
        }
        if (type == "vec2")
        {
            outType = ExposedVariableType::Vec2;
            return true;
        }
        if (type == "vec3")
        {
            outType = ExposedVariableType::Vec3;
            return true;
        }
        if (type == "vec4")
        {
            outType = ExposedVariableType::Vec4;
            return true;
        }
        if (type == "entity")
        {
            outType = ExposedVariableType::Entity;
            return true;
        }
        return false;
    }

    bool scriptVariableFromJson(const nlohmann::json &jsonValue, elix::engine::Script::ExposedVariable &outVariable)
    {
        if (!jsonValue.is_object())
            return false;

        const std::string typeString = jsonValue.value("type", std::string{});
        if (typeString.empty() || !jsonValue.contains("value"))
            return false;

        elix::engine::Script::ExposedVariableType type{};
        if (!scriptVariableTypeFromString(typeString, type))
            return false;

        outVariable.type = type;
        const auto &valueJson = jsonValue["value"];

        using ExposedVariableType = elix::engine::Script::ExposedVariableType;
        switch (type)
        {
        case ExposedVariableType::Bool:
            if (!valueJson.is_boolean())
                return false;
            outVariable.value = valueJson.get<bool>();
            return true;
        case ExposedVariableType::Int:
            if (!valueJson.is_number_integer())
                return false;
            outVariable.value = valueJson.get<int32_t>();
            return true;
        case ExposedVariableType::Float:
            if (!valueJson.is_number())
                return false;
            outVariable.value = valueJson.get<float>();
            return true;
        case ExposedVariableType::String:
            if (!valueJson.is_string())
                return false;
            outVariable.value = valueJson.get<std::string>();
            return true;
        case ExposedVariableType::Vec2:
            if (!valueJson.is_array() || valueJson.size() != 2)
                return false;
            outVariable.value = glm::vec2(valueJson[0].get<float>(), valueJson[1].get<float>());
            return true;
        case ExposedVariableType::Vec3:
            if (!valueJson.is_array() || valueJson.size() != 3)
                return false;
            outVariable.value = glm::vec3(valueJson[0].get<float>(), valueJson[1].get<float>(), valueJson[2].get<float>());
            return true;
        case ExposedVariableType::Vec4:
            if (!valueJson.is_array() || valueJson.size() != 4)
                return false;
            outVariable.value = glm::vec4(valueJson[0].get<float>(), valueJson[1].get<float>(), valueJson[2].get<float>(), valueJson[3].get<float>());
            return true;
        case ExposedVariableType::Entity:
            if (valueJson.is_null())
            {
                outVariable.value = Script::EntityRef{};
                return true;
            }

            if (valueJson.is_number_unsigned())
            {
                outVariable.value = Script::EntityRef(valueJson.get<uint32_t>());
                return true;
            }

            if (valueJson.is_number_integer())
            {
                const int64_t signedId = valueJson.get<int64_t>();
                if (signedId < 0 || signedId > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
                    return false;

                outVariable.value = Script::EntityRef(static_cast<uint32_t>(signedId));
                return true;
            }

            return false;
        default:
            return false;
        }
    }

    nlohmann::json scriptVariableToJson(const elix::engine::Script::ExposedVariable &variable)
    {
        nlohmann::json jsonValue;
        jsonValue["type"] = scriptVariableTypeToString(variable.type);

        std::visit(
            [&](const auto &value)
            {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, glm::vec2>)
                    jsonValue["value"] = {value.x, value.y};
                else if constexpr (std::is_same_v<T, glm::vec3>)
                    jsonValue["value"] = {value.x, value.y, value.z};
                else if constexpr (std::is_same_v<T, glm::vec4>)
                    jsonValue["value"] = {value.x, value.y, value.z, value.w};
                else if constexpr (std::is_same_v<T, Script::EntityRef>)
                    jsonValue["value"] = value.isValid() ? nlohmann::json(value.id) : nlohmann::json(nullptr);
                else
                    jsonValue["value"] = value;
            },
            variable.value);

        return jsonValue;
    }

    glm::quat worldRotationFromForward(const glm::vec3 &forward)
    {
        constexpr float epsilon = std::numeric_limits<float>::epsilon();

        const float directionLength = glm::length(forward);
        if (directionLength <= epsilon)
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        const glm::vec3 normalizedForward = forward / directionLength;
        return glm::rotation(glm::vec3(0.0f, 0.0f, -1.0f), normalizedForward);
    }
}

Scene::Scene() : m_physicsScene(PhysXCore::getInstance()->getPhysics())
{
}

Scene::~Scene()
{
    // Ensure components are destroyed while physics scene/resources are still valid.
    m_entities.clear();
}

PhysicsScene &Scene::getPhysicsScene()
{
    return m_physicsScene;
}

void Scene::setSkyboxHDRPath(const std::string &path)
{
    m_skyboxHDRPath = path;
}

const std::string &Scene::getSkyboxHDRPath() const
{
    return m_skyboxHDRPath;
}

bool Scene::hasSkyboxHDR() const
{
    return !m_skyboxHDRPath.empty();
}

void Scene::clearSkyboxHDR()
{
    m_skyboxHDRPath.clear();
}

ui::UIText *Scene::addUIText()
{
    m_uiTexts.push_back(std::make_unique<ui::UIText>());
    return m_uiTexts.back().get();
}

ui::UIButton *Scene::addUIButton()
{
    m_uiButtons.push_back(std::make_unique<ui::UIButton>());
    return m_uiButtons.back().get();
}

ui::Billboard *Scene::addBillboard()
{
    m_billboards.push_back(std::make_unique<ui::Billboard>());
    return m_billboards.back().get();
}

void Scene::removeUIText(const ui::UIText *text)
{
    m_uiTexts.erase(std::remove_if(m_uiTexts.begin(), m_uiTexts.end(),
                                   [text](const std::unique_ptr<ui::UIText> &p)
                                   { return p.get() == text; }),
                    m_uiTexts.end());
}

void Scene::removeUIButton(const ui::UIButton *button)
{
    m_uiButtons.erase(std::remove_if(m_uiButtons.begin(), m_uiButtons.end(),
                                     [button](const std::unique_ptr<ui::UIButton> &p)
                                     { return p.get() == button; }),
                      m_uiButtons.end());
}

void Scene::removeBillboard(const ui::Billboard *billboard)
{
    m_billboards.erase(std::remove_if(m_billboards.begin(), m_billboards.end(),
                                      [billboard](const std::unique_ptr<ui::Billboard> &p)
                                      { return p.get() == billboard; }),
                       m_billboards.end());
}

const std::vector<std::unique_ptr<ui::UIText>> &Scene::getUITexts() const { return m_uiTexts; }
const std::vector<std::unique_ptr<ui::UIButton>> &Scene::getUIButtons() const { return m_uiButtons; }
const std::vector<std::unique_ptr<ui::Billboard>> &Scene::getBillboards() const { return m_billboards; }

const std::vector<Entity::SharedPtr> &Scene::getEntities() const
{
    return m_entities;
}

Scene::SharedPtr Scene::copy()
{
    auto copiedScene = std::make_shared<Scene>();

    std::error_code tempDirectoryError;
    std::filesystem::path tempDirectory = std::filesystem::temp_directory_path(tempDirectoryError);
    if (tempDirectoryError)
        tempDirectory = std::filesystem::current_path();

    const auto timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::filesystem::path tempScenePath =
        tempDirectory / ("velix_scene_copy_" + std::to_string(timestamp) + "_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".elixscene");

    saveSceneToFile(tempScenePath.string());
    const bool loadedSuccessfully = copiedScene->loadSceneFromFile(tempScenePath.string());

    std::error_code removeError;
    std::filesystem::remove(tempScenePath, removeError);

    if (!loadedSuccessfully)
    {
        VX_ENGINE_ERROR_STREAM("Scene::copy failed to load temporary scene snapshot: " << tempScenePath << '\n');
        return nullptr;
    }

    return copiedScene;
}

Entity *Scene::getEntityById(uint32_t id)
{
    for (const auto &entity : m_entities)
        if (entity->getId() == id)
            return entity.get();

    return nullptr;
}

bool Scene::doesEntityNameExist(const std::string &name) const
{
    for (auto &&entity : m_entities)
        if (entity->getName() == name)
            return true;

    return false;
}

Entity::SharedPtr Scene::addEntity(Entity &en, const std::string &name)
{
    auto generateUniqueName = [this](const std::string &baseName)
    {
        int counter = 1;
        std::string newName;

        do
        {
            newName = baseName + "_" + (counter < 10 ? "0" : "") + std::to_string(counter);
            counter++;
        } while (doesEntityNameExist(newName));

        return newName;
    };

    std::string actualName = name;
    if (doesEntityNameExist(name))
        actualName = generateUniqueName(name);

    auto entity = std::make_shared<Entity>(en, actualName, m_nextEntityId);
    entity->addComponent<Transform3DComponent>();
    if (m_nextEntityId < std::numeric_limits<uint32_t>::max())
        ++m_nextEntityId;

    m_entities.push_back(entity);
    return entity;
}

Entity::SharedPtr Scene::addEntity(const std::string &name)
{
    auto generateUniqueName = [this](const std::string &baseName)
    {
        int counter = 1;
        std::string newName;

        do
        {
            newName = baseName + "_" + (counter < 10 ? "0" : "") + std::to_string(counter);
            counter++;
        } while (doesEntityNameExist(newName));

        return newName;
    };

    std::string actualName = name;
    if (doesEntityNameExist(name))
        actualName = generateUniqueName(name);

    auto entity = std::make_shared<Entity>(actualName);

    entity->addComponent<Transform3DComponent>();
    entity->setId(m_nextEntityId);
    if (m_nextEntityId < std::numeric_limits<uint32_t>::max())
        ++m_nextEntityId;

    m_entities.push_back(entity);
    return entity;
}

bool Scene::loadSceneFromFile(const std::string &filePath, const LoadStatusCallback &statusCallback)
{
    auto reportStatus = [&](const std::string &status)
    {
        if (statusCallback)
            statusCallback(status);
    };

    reportStatus("Resetting scene state...");

    m_entities.clear();
    m_uiTexts.clear();
    m_uiButtons.clear();
    m_billboards.clear();
    m_nextEntityId = 0;
    m_skyboxHDRPath.clear();

    reportStatus("Opening scene file...");

    std::ifstream file(filePath);

    if (!file.is_open())
    {
        VX_ENGINE_ERROR_STREAM("Failed to open file: " << filePath << std::endl);
        ;
        reportStatus("Failed to open scene file");
        return false;
    }

    nlohmann::json json;

    reportStatus("Parsing scene data...");

    try
    {
        file >> json;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        VX_ENGINE_ERROR_STREAM("Failed to parse scene file " << e.what() << std::endl);
        reportStatus("Failed to parse scene JSON");
        return false;
    }

    if (json.contains("name"))
    {
        m_name = json["name"];
    }

    const std::filesystem::path sceneDirectory = std::filesystem::path(filePath).parent_path();
    auto resolveScenePath = [&](const std::string &rawPath) -> std::string
    {
        if (rawPath.empty())
            return {};

        std::filesystem::path path(rawPath);
        if (path.is_relative())
            path = sceneDirectory / path;

        return path.lexically_normal().string();
    };

    if (json.contains("environment") && json["environment"].is_object())
    {
        reportStatus("Loading environment...");
        const std::string skyboxHDR = json["environment"].value("skybox_hdr", std::string{});
        m_skyboxHDRPath = resolveScenePath(skyboxHDR);
    }
    else if (json.contains("enviroment"))
    {
        reportStatus("Loading environment...");
        for (const auto &environmentObject : json["enviroment"])
        {
            if (!environmentObject.is_object())
                continue;

            if (environmentObject.contains("skybox_hdr") && environmentObject["skybox_hdr"].is_string())
            {
                m_skyboxHDRPath = resolveScenePath(environmentObject["skybox_hdr"].get<std::string>());
                break;
            }

            if (environmentObject.contains("skybox") && environmentObject["skybox"].is_string())
            {
                m_skyboxHDRPath = resolveScenePath(environmentObject["skybox"].get<std::string>());
                break;
            }
        }
    }

    if (json.contains("render_settings") && json["render_settings"].is_object())
    {
        reportStatus("Applying render settings...");
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

        if (renderSettingsJson.contains("enable_post_processing") && renderSettingsJson["enable_post_processing"].is_boolean())
            settings.enablePostProcessing = renderSettingsJson["enable_post_processing"].get<bool>();
        if (renderSettingsJson.contains("enable_vsync") && renderSettingsJson["enable_vsync"].is_boolean())
            settings.enableVSync = renderSettingsJson["enable_vsync"].get<bool>();

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
            settings.anisotropyStrength = renderSettingsJson["anisotropy_strength"].get<float>();
        if (renderSettingsJson.contains("anisotropy_rotation") && renderSettingsJson["anisotropy_rotation"].is_number())
            settings.anisotropyRotation = renderSettingsJson["anisotropy_rotation"].get<float>();
        if (renderSettingsJson.contains("shadow_ambient_strength") && renderSettingsJson["shadow_ambient_strength"].is_number())
            settings.shadowAmbientStrength = renderSettingsJson["shadow_ambient_strength"].get<float>();
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

        if (renderSettingsJson.contains("enable_lut_grading") && renderSettingsJson["enable_lut_grading"].is_boolean())
            settings.enableLUTGrading = renderSettingsJson["enable_lut_grading"].get<bool>();
        if (renderSettingsJson.contains("lut_grading_path") && renderSettingsJson["lut_grading_path"].is_string())
            settings.lutGradingPath = resolveScenePath(renderSettingsJson["lut_grading_path"].get<std::string>());
        if (renderSettingsJson.contains("lut_grading_strength") && renderSettingsJson["lut_grading_strength"].is_number())
            settings.lutGradingStrength = renderSettingsJson["lut_grading_strength"].get<float>();

        if (renderSettingsJson.contains("enable_bloom") && renderSettingsJson["enable_bloom"].is_boolean())
            settings.enableBloom = renderSettingsJson["enable_bloom"].get<bool>();

        if (renderSettingsJson.contains("enable_ssr") && renderSettingsJson["enable_ssr"].is_boolean())
            settings.enableSSR = renderSettingsJson["enable_ssr"].get<bool>();

        if (renderSettingsJson.contains("enable_ibl") && renderSettingsJson["enable_ibl"].is_boolean())
            settings.enableIBL = renderSettingsJson["enable_ibl"].get<bool>();
        if (renderSettingsJson.contains("ibl_diffuse_intensity") && renderSettingsJson["ibl_diffuse_intensity"].is_number())
            settings.iblDiffuseIntensity = std::clamp(renderSettingsJson["ibl_diffuse_intensity"].get<float>(), 0.0f, 3.0f);
        if (renderSettingsJson.contains("ibl_specular_intensity") && renderSettingsJson["ibl_specular_intensity"].is_number())
            settings.iblSpecularIntensity = std::clamp(renderSettingsJson["ibl_specular_intensity"].get<float>(), 0.0f, 3.0f);

        if (renderSettingsJson.contains("render_scale") && renderSettingsJson["render_scale"].is_number())
            settings.renderScale = std::clamp(renderSettingsJson["render_scale"].get<float>(), 0.25f, 2.0f);
    }

    if (json.contains("ui_objects") && json["ui_objects"].is_array())
    {
        const size_t uiObjectCount = json["ui_objects"].size();
        if (uiObjectCount > 0)
            reportStatus("Loading UI objects (0/" + std::to_string(uiObjectCount) + ")...");

        auto parseVec2 = [](const nlohmann::json &arrayJson, const glm::vec2 &fallback) -> glm::vec2
        {
            if (!arrayJson.is_array() || arrayJson.size() != 2 || !arrayJson[0].is_number() || !arrayJson[1].is_number())
                return fallback;

            return glm::vec2(arrayJson[0].get<float>(), arrayJson[1].get<float>());
        };

        auto parseVec3 = [](const nlohmann::json &arrayJson, const glm::vec3 &fallback) -> glm::vec3
        {
            if (!arrayJson.is_array() || arrayJson.size() != 3 ||
                !arrayJson[0].is_number() || !arrayJson[1].is_number() || !arrayJson[2].is_number())
                return fallback;

            return glm::vec3(arrayJson[0].get<float>(), arrayJson[1].get<float>(), arrayJson[2].get<float>());
        };

        auto parseVec4 = [](const nlohmann::json &arrayJson, const glm::vec4 &fallback) -> glm::vec4
        {
            if (!arrayJson.is_array() || arrayJson.size() != 4 ||
                !arrayJson[0].is_number() || !arrayJson[1].is_number() || !arrayJson[2].is_number() || !arrayJson[3].is_number())
                return fallback;

            return glm::vec4(arrayJson[0].get<float>(), arrayJson[1].get<float>(), arrayJson[2].get<float>(), arrayJson[3].get<float>());
        };

        size_t uiObjectIndex = 0;
        for (const auto &uiObjectJson : json["ui_objects"])
        {
            ++uiObjectIndex;
            if ((uiObjectIndex == uiObjectCount) || ((uiObjectIndex % 16u) == 0u))
                reportStatus("Loading UI objects (" + std::to_string(uiObjectIndex) + "/" + std::to_string(uiObjectCount) + ")...");

            if (!uiObjectJson.is_object())
                continue;

            const std::string type = uiObjectJson.value("type", std::string{});
            if (type == "text")
            {
                auto *text = addUIText();
                if (!text)
                    continue;

                text->setEnabled(uiObjectJson.value("enabled", true));
                text->setText(uiObjectJson.value("text", std::string{}));
                text->setPosition(parseVec2(uiObjectJson.value("position", nlohmann::json::array()), text->getPosition()));
                text->setScale(uiObjectJson.value("scale", text->getScale()));
                text->setRotation(uiObjectJson.value("rotation", text->getRotation()));
                text->setColor(parseVec4(uiObjectJson.value("color", nlohmann::json::array()), text->getColor()));

                const std::string fontPath = resolveScenePath(uiObjectJson.value("font_path", std::string{}));
                if (!fontPath.empty())
                    text->loadFont(fontPath);
            }
            else if (type == "button")
            {
                auto *button = addUIButton();
                if (!button)
                    continue;

                button->setEnabled(uiObjectJson.value("enabled", true));
                button->setPosition(parseVec2(uiObjectJson.value("position", nlohmann::json::array()), button->getPosition()));
                button->setSize(parseVec2(uiObjectJson.value("size", nlohmann::json::array()), button->getSize()));
                button->setBackgroundColor(parseVec4(uiObjectJson.value("background_color", nlohmann::json::array()), button->getBackgroundColor()));
                button->setHoverColor(parseVec4(uiObjectJson.value("hover_color", nlohmann::json::array()), button->getHoverColor()));
                button->setBorderColor(parseVec4(uiObjectJson.value("border_color", nlohmann::json::array()), button->getBorderColor()));
                button->setBorderWidth(uiObjectJson.value("border_width", button->getBorderWidth()));
                button->setLabel(uiObjectJson.value("label", std::string{}));
                button->setLabelColor(parseVec4(uiObjectJson.value("label_color", nlohmann::json::array()), button->getLabelColor()));
                button->setLabelScale(uiObjectJson.value("label_scale", button->getLabelScale()));
                button->setRotation(uiObjectJson.value("rotation", button->getRotation()));

                const std::string fontPath = resolveScenePath(uiObjectJson.value("font_path", std::string{}));
                if (!fontPath.empty())
                    button->loadFont(fontPath);
            }
            else if (type == "billboard")
            {
                auto *billboard = addBillboard();
                if (!billboard)
                    continue;

                billboard->setEnabled(uiObjectJson.value("enabled", true));
                billboard->setWorldPosition(parseVec3(uiObjectJson.value("world_position", nlohmann::json::array()), billboard->getWorldPosition()));
                billboard->setSize(uiObjectJson.value("size", billboard->getSize()));
                billboard->setRotation(uiObjectJson.value("rotation", billboard->getRotation()));
                billboard->setColor(parseVec4(uiObjectJson.value("color", nlohmann::json::array()), billboard->getColor()));

                const std::string texturePath = resolveScenePath(uiObjectJson.value("texture_path", std::string{}));
                if (!texturePath.empty())
                    billboard->setTexturePath(texturePath);
            }
        }
    }

    // Restore material override paths on a mesh component (GPU material loading is deferred to editor layer)
    auto restoreMaterialOverrides = [&](auto *meshComp, const nlohmann::json &overridesJson)
    {
        if (!meshComp || !overridesJson.is_array())
            return;
        for (const auto &ovJson : overridesJson)
        {
            if (!ovJson.contains("slot") || !ovJson.contains("path"))
                continue;
            const size_t slot = ovJson["slot"].get<size_t>();
            if (slot >= meshComp->getMaterialSlotCount())
                continue;
            const std::string matPath = resolveScenePath(ovJson["path"].get<std::string>());
            if (!matPath.empty())
                meshComp->setMaterialOverridePath(slot, matPath);
        }
    };

    if (json.contains("game_objects"))
    {
        const size_t gameObjectCount = json["game_objects"].size();
        if (gameObjectCount > 0)
            reportStatus("Loading game objects (0/" + std::to_string(gameObjectCount) + ")...");

        std::unordered_map<uint32_t, Entity *> entitiesById;
        std::vector<std::pair<Entity *, uint32_t>> pendingParents;
        std::vector<std::pair<Entity *, glm::vec3>> pendingLightDirections;

        struct AnimatorState
        {
            Entity *entity;
            int selectedAnim;
            float speed;
            bool looped;
            bool paused;
        };
        std::vector<AnimatorState> pendingAnimatorStates;

        size_t gameObjectIndex = 0;
        for (const auto &objectJson : json["game_objects"])
        {
            ++gameObjectIndex;
            if ((gameObjectIndex == gameObjectCount) || ((gameObjectIndex % 8u) == 0u))
                reportStatus("Loading game objects (" + std::to_string(gameObjectIndex) + "/" + std::to_string(gameObjectCount) + ")...");

            const std::string &name = objectJson.value("name", "undefined");

            auto gameObject = addEntity(name);

            if (objectJson.contains("id"))
            {
                const uint32_t objectId = objectJson["id"];
                gameObject->setId(objectId);
                entitiesById[objectId] = gameObject.get();

                const uint32_t candidateNextId =
                    (objectId == std::numeric_limits<uint32_t>::max()) ? objectId : static_cast<uint32_t>(objectId + 1u);
                m_nextEntityId = std::max(m_nextEntityId, candidateNextId);
            }
            else
                entitiesById[gameObject->getId()] = gameObject.get();

            gameObject->setEnabled(objectJson.value("enabled", true));

            if (objectJson.contains("parent_id"))
                pendingParents.emplace_back(gameObject.get(), objectJson["parent_id"]);

            auto transformation = gameObject->getComponent<Transform3DComponent>();

            if (objectJson.contains("position"))
            {
                const auto &pos = objectJson["position"];
                transformation->setPosition({pos[0], pos[1], pos[2]});
            }

            if (objectJson.contains("scale"))
            {
                const auto &scale = objectJson["scale"];
                transformation->setScale({scale[0], scale[1], scale[2]});
            }

            if (objectJson.contains("rotation"))
            {
                const auto &rot = objectJson["rotation"];
                transformation->setEulerDegrees({rot[0], rot[1], rot[2]});
            }

            if (objectJson.contains("tags") && objectJson["tags"].is_array())
            {
                for (const auto &tag : objectJson["tags"])
                {
                    if (tag.is_string())
                        gameObject->addTag(tag.get<std::string>());
                }
            }

            // Determine if the JSON has an explicit mesh component
            bool hasMeshComponent = false;
            if (objectJson.contains("components") && objectJson["components"].is_array())
            {
                for (const auto &c : objectJson["components"])
                {
                    if (!c.contains("type"))
                        continue;
                    const std::string t = c["type"];
                    if (t == "static_mesh" || t == "skeletal_mesh")
                    {
                        hasMeshComponent = true;
                        break;
                    }
                }
            }

            // Backward compat: old scenes with has_legacy_mesh but no explicit mesh component
            if (!hasMeshComponent && objectJson.value("has_legacy_mesh", false))
            {
                CPUMesh mesh = CPUMesh::build<vertex::Vertex3D>(cube::vertices, cube::indices);
                mesh.name = "Cube";
                gameObject->addComponent<StaticMeshComponent>(std::vector<CPUMesh>{mesh});
            }

            if (!objectJson.contains("components"))
                continue;

            for (const auto &componentJson : objectJson["components"])
            {
                if (!componentJson.contains("type"))
                    continue;

                const std::string type = componentJson["type"];

                if (type == "static_mesh")
                {
                    std::vector<CPUMesh> meshes;
                    std::string assetPath;

                    if (componentJson.value("is_primitive", false))
                    {
                        const std::string primType = componentJson.value("primitive_type", "Cube");
                        if (primType == "Sphere")
                        {
                            std::vector<vertex::Vertex3D> verts;
                            std::vector<uint32_t> inds;
                            circle::genereteVerticesAndIndices(verts, inds);
                            auto mesh = CPUMesh::build<vertex::Vertex3D>(verts, inds);
                            mesh.name = "Sphere";
                            meshes.push_back(mesh);
                        }
                        else
                        {
                            auto mesh = CPUMesh::build<vertex::Vertex3D>(cube::vertices, cube::indices);
                            mesh.name = "Cube";
                            meshes.push_back(mesh);
                        }
                    }
                    else
                    {
                        assetPath = resolveScenePath(componentJson.value("asset_path", std::string{}));
                        if (!assetPath.empty())
                        {
                            auto modelAsset = AssetsLoader::loadModel(assetPath);
                            if (modelAsset.has_value())
                                meshes = modelAsset->meshes;
                            else
                                VX_ENGINE_WARNING_STREAM("Failed to load model for static_mesh: " << assetPath << '\n');
                        }
                    }

                    if (!meshes.empty())
                    {
                        auto *sm = gameObject->addComponent<StaticMeshComponent>(meshes);
                        sm->setAssetPath(assetPath);
                        restoreMaterialOverrides(sm, componentJson.value("material_overrides", nlohmann::json::array()));
                    }
                }
                else if (type == "skeletal_mesh")
                {
                    const std::string assetPath = resolveScenePath(componentJson.value("asset_path", std::string{}));
                    if (!assetPath.empty())
                    {
                        auto modelAsset = AssetsLoader::loadModel(assetPath);
                        if (modelAsset.has_value() && modelAsset->skeleton.has_value())
                        {
                            auto *skm = gameObject->addComponent<SkeletalMeshComponent>(
                                modelAsset->meshes, modelAsset->skeleton.value());
                            skm->setAssetPath(assetPath);
                            restoreMaterialOverrides(skm, componentJson.value("material_overrides", nlohmann::json::array()));

                            if (!modelAsset->animations.empty())
                            {
                                auto *anim = gameObject->addComponent<AnimatorComponent>();
                                anim->setAnimations(modelAsset->animations, &skm->getSkeleton());
                            }
                        }
                        else
                            VX_ENGINE_WARNING_STREAM("Failed to load skeletal model: " << assetPath << '\n');
                    }
                }
                else if (type == "animator")
                {
                    pendingAnimatorStates.push_back({gameObject.get(),
                                                     componentJson.value("selected_animation", -1),
                                                     componentJson.value("speed", 1.0f),
                                                     componentJson.value("looped", true),
                                                     componentJson.value("paused", false)});
                }
                else if (type == "camera")
                {
                    auto *cameraComponent = gameObject->addComponent<CameraComponent>();
                    if (!cameraComponent)
                        continue;

                    const auto camera = cameraComponent->getCamera();
                    if (!camera)
                        continue;

                    camera->setYaw(componentJson.value("yaw", camera->getYaw()));
                    camera->setPitch(componentJson.value("pitch", camera->getPitch()));
                    camera->setFOV(componentJson.value("fov", camera->getFOV()));
                    camera->setAspect(componentJson.value("aspect", camera->getAspect()));

                    bool hasExplicitOffset = false;
                    if (componentJson.contains("position_offset") &&
                        componentJson["position_offset"].is_array() &&
                        componentJson["position_offset"].size() == 3)
                    {
                        const auto &offset = componentJson["position_offset"];
                        cameraComponent->setPositionOffset({offset[0], offset[1], offset[2]});
                        hasExplicitOffset = true;
                    }

                    if (componentJson.contains("position") &&
                        componentJson["position"].is_array() &&
                        componentJson["position"].size() == 3)
                    {
                        const auto &position = componentJson["position"];
                        // Backward compatibility for old scenes where camera position
                        // lived in component data instead of entity transform.
                        if (!objectJson.contains("position"))
                            transformation->setPosition({position[0], position[1], position[2]});
                        else if (!hasExplicitOffset)
                        {
                            const glm::vec3 basePosition = transformation->getWorldPosition();
                            cameraComponent->setPositionOffset(glm::vec3{position[0], position[1], position[2]} - basePosition);
                        }
                    }

                    cameraComponent->syncFromOwnerTransform();
                }
                else if (type == "light")
                {
                    LightComponent::LightType lightType{LightComponent::LightType::NONE};
                    const std::string stringLightType = componentJson.value("light_type", std::string{});

                    if (stringLightType == "directional")
                        lightType = LightComponent::LightType::DIRECTIONAL;
                    else if (stringLightType == "spot")
                        lightType = LightComponent::LightType::SPOT;
                    else if (stringLightType == "point")
                        lightType = LightComponent::LightType::POINT;

                    if (lightType == LightComponent::LightType::NONE)
                    {
                        VX_ENGINE_ERROR_STREAM("Light type is none\n");
                        continue;
                    }

                    LightComponent *lightComponent = gameObject->addComponent<LightComponent>(lightType);
                    auto light = lightComponent->getLight();

                    if (componentJson.contains("color"))
                    {
                        const auto &color = componentJson["color"];
                        light->color = {color[0], color[1], color[2]};
                    }

                    if (componentJson.contains("position"))
                    {
                        const auto &position = componentJson["position"];
                        if (!objectJson.contains("position"))
                            transformation->setPosition({position[0], position[1], position[2]});
                    }

                    if (componentJson.contains("strength"))
                        light->strength = componentJson["strength"];

                    light->castsShadows = componentJson.value("casts_shadows", light->castsShadows);

                    if (componentJson.contains("direction"))
                    {
                        const auto &direction = componentJson["direction"];
                        pendingLightDirections.emplace_back(gameObject.get(), glm::vec3{direction[0], direction[1], direction[2]});
                    }

                    if (lightType == LightComponent::LightType::DIRECTIONAL)
                    {
                        if (auto *dl = dynamic_cast<DirectionalLight *>(light.get()))
                            dl->skyLightEnabled = componentJson.value("sky_light_enabled", true);
                    }
                    else if (lightType == LightComponent::LightType::POINT)
                    {
                        if (auto *pl = dynamic_cast<PointLight *>(light.get()))
                        {
                            pl->radius = componentJson.value("radius", pl->radius);
                            pl->falloff = componentJson.value("falloff", pl->falloff);
                        }
                    }
                    else if (lightType == LightComponent::LightType::SPOT)
                    {
                        if (auto *sl = dynamic_cast<SpotLight *>(light.get()))
                        {
                            sl->innerAngle = componentJson.value("inner_angle", sl->innerAngle);
                            sl->outerAngle = componentJson.value("outer_angle", sl->outerAngle);
                            sl->range = componentJson.value("range", sl->range);
                        }
                    }
                }
                else if (type == "rigid_body")
                {
                    const glm::vec3 worldPos = transformation->getWorldPosition();
                    const glm::quat worldRot = transformation->getWorldRotation();
                    auto *dynActor = m_physicsScene.createDynamic(
                        physx::PxTransform(
                            physx::PxVec3(worldPos.x, worldPos.y, worldPos.z),
                            physx::PxQuat(worldRot.x, worldRot.y, worldRot.z, worldRot.w)));

                    if (dynActor)
                    {
                        auto *rb = gameObject->addComponent<RigidBodyComponent>(dynActor);
                        rb->setKinematic(componentJson.value("is_kinematic", false));
                        rb->setGravityEnable(componentJson.value("gravity_enabled", true));
                    }
                }
                else if (type == "collision")
                {
                    std::string collisionType = componentJson.value("collision_type", "box");
                    std::transform(collisionType.begin(), collisionType.end(), collisionType.begin(), ::tolower);

                    CollisionComponent::ShapeType shapeType = CollisionComponent::ShapeType::BOX;
                    glm::vec3 boxHalfExtents(0.5f);
                    float capsuleRadius = 0.5f;
                    float capsuleHalfHeight = 0.5f;
                    physx::PxShape *shape = nullptr;

                    if (collisionType == "capsule")
                    {
                        shapeType = CollisionComponent::ShapeType::CAPSULE;
                        capsuleRadius = std::max(componentJson.value("radius", 0.5f), 0.01f);
                        capsuleHalfHeight = std::max(componentJson.value("half_height", 0.5f), 0.0f);
                        shape = m_physicsScene.createShape(physx::PxCapsuleGeometry(capsuleRadius, capsuleHalfHeight));
                        if (shape)
                            shape->setLocalPose(physx::PxTransform(physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0.0f, 0.0f, 1.0f))));
                    }
                    else
                    {
                        if (componentJson.contains("half_extents") &&
                            componentJson["half_extents"].is_array() &&
                            componentJson["half_extents"].size() == 3)
                        {
                            boxHalfExtents.x = componentJson["half_extents"][0];
                            boxHalfExtents.y = componentJson["half_extents"][1];
                            boxHalfExtents.z = componentJson["half_extents"][2];
                        }
                        boxHalfExtents = glm::max(boxHalfExtents, glm::vec3(0.01f));
                        shape = m_physicsScene.createShape(physx::PxBoxGeometry(boxHalfExtents.x, boxHalfExtents.y, boxHalfExtents.z));
                    }

                    if (!shape)
                    {
                        VX_ENGINE_ERROR_STREAM("Failed to create collision shape while loading scene\n");
                        continue;
                    }

                    if (auto *rb = gameObject->getComponent<RigidBodyComponent>())
                    {
                        rb->getRigidActor()->attachShape(*shape);
                        if (auto *dyn = rb->getRigidActor()->is<physx::PxRigidDynamic>())
                            physx::PxRigidBodyExt::updateMassAndInertia(*dyn, 10.0f);

                        gameObject->addComponent<CollisionComponent>(shape, shapeType, boxHalfExtents, capsuleRadius, capsuleHalfHeight, nullptr);
                    }
                    else
                    {
                        const glm::vec3 worldPosition = transformation->getWorldPosition();
                        const glm::quat worldRotation = transformation->getWorldRotation();
                        auto *staticActor = m_physicsScene.createStatic(
                            physx::PxTransform(
                                physx::PxVec3(worldPosition.x, worldPosition.y, worldPosition.z),
                                physx::PxQuat(worldRotation.x, worldRotation.y, worldRotation.z, worldRotation.w)));

                        staticActor->attachShape(*shape);
                        gameObject->addComponent<CollisionComponent>(shape, shapeType, boxHalfExtents, capsuleRadius, capsuleHalfHeight, staticActor);
                    }
                }
                else if (type == "character_movement")
                {
                    const float capsuleRadius = std::max(componentJson.value("radius", 0.35f), 0.05f);
                    const float capsuleHeight = std::max(componentJson.value("height", 1.0f), 0.1f);

                    auto *characterMovement = gameObject->addComponent<CharacterMovementComponent>(this, capsuleRadius, capsuleHeight);
                    if (!characterMovement)
                        continue;

                    characterMovement->setStepOffset(componentJson.value("step_offset", characterMovement->getStepOffset()));
                    characterMovement->setContactOffset(componentJson.value("contact_offset", characterMovement->getContactOffset()));
                    characterMovement->setSlopeLimitDegrees(componentJson.value("slope_limit_degrees", characterMovement->getSlopeLimitDegrees()));
                }
                else if (type == "audio")
                {
                    auto *audio = gameObject->addComponent<AudioComponent>();
                    const std::string assetPath = resolveScenePath(componentJson.value("asset_path", std::string{}));
                    if (!assetPath.empty())
                        audio->loadFromAsset(assetPath);

                    audio->setVolume(componentJson.value("volume", 1.0f));
                    audio->setPitch(componentJson.value("pitch", 1.0f));
                    audio->setLooping(componentJson.value("loop", false));
                    audio->setPlayOnStart(componentJson.value("play_on_start", false));
                    audio->setMuted(componentJson.value("muted", false));
                    audio->setSpatial(componentJson.value("spatial", false));
                    audio->setMinDistance(componentJson.value("min_distance", 1.0f));
                    audio->setMaxDistance(componentJson.value("max_distance", 500.0f));

                    const std::string audioTypeStr = componentJson.value("audio_type", "sound");
                    audio->setAudioType(audioTypeStr == "music" ? AudioComponent::AudioType::Music
                                                                : AudioComponent::AudioType::Sound);
                }
                else if (type == "script")
                {
                    const std::string scriptName = componentJson.value("name", std::string{});
                    if (!scriptName.empty())
                    {
                        Script *script = ScriptsRegister::createScriptFromActiveRegister(scriptName);
                        if (script)
                        {
                            auto *scriptComponent = gameObject->addComponent<ScriptComponent>(scriptName, script);

                            if (scriptComponent &&
                                componentJson.contains("variables") &&
                                componentJson["variables"].is_object())
                            {
                                Script::ExposedVariablesMap serializedVariables;
                                for (auto it = componentJson["variables"].begin(); it != componentJson["variables"].end(); ++it)
                                {
                                    Script::ExposedVariable variable;
                                    if (!scriptVariableFromJson(it.value(), variable))
                                        continue;

                                    serializedVariables[it.key()] = std::move(variable);
                                }

                                scriptComponent->setSerializedVariables(serializedVariables);
                            }
                        }
                        else
                            VX_ENGINE_WARNING_STREAM("Script not found in registry: " << scriptName << '\n');
                    }
                }
                else if (type == "particle_system")
                {
                    auto ps = std::make_shared<ParticleSystem>();

                    if (componentJson.contains("system") && componentJson["system"].is_object())
                    {
                        const auto &sysJson = componentJson["system"];
                        ps->name = sysJson.value("name", "Particle System");

                        if (sysJson.contains("emitters") && sysJson["emitters"].is_array())
                        {
                            for (const auto &emJson : sysJson["emitters"])
                            {
                                auto *emitter = ps->addEmitter(emJson.value("name", "Emitter"));
                                emitter->enabled = emJson.value("enabled", true);

                                if (!emJson.contains("modules"))
                                    continue;
                                const auto &mods = emJson["modules"];

                                if (mods.contains("spawn"))
                                {
                                    const auto &m = mods["spawn"];
                                    auto *spawn = emitter->addModule<SpawnModule>();
                                    spawn->setEnabled(m.value("enabled", true));
                                    spawn->spawnRate = m.value("spawn_rate", 100.0f);
                                    spawn->burstCount = m.value("burst_count", 0.0f);
                                    spawn->loop = m.value("loop", true);
                                    spawn->duration = m.value("duration", 5.0f);

                                    if (m.contains("shape") && m["shape"].is_object())
                                    {
                                        const auto &sh = m["shape"];
                                        const std::string shapeStr = sh.value("type", "point");

                                        if (shapeStr == "sphere")
                                            spawn->shape.shape = EmitterShape::Sphere;
                                        else if (shapeStr == "box")
                                            spawn->shape.shape = EmitterShape::Box;
                                        else if (shapeStr == "cone")
                                            spawn->shape.shape = EmitterShape::Cone;
                                        else if (shapeStr == "cylinder")
                                            spawn->shape.shape = EmitterShape::Cylinder;
                                        else
                                            spawn->shape.shape = EmitterShape::Point;

                                        if (sh.contains("extents") && sh["extents"].is_array() && sh["extents"].size() == 3)
                                            spawn->shape.extents = {sh["extents"][0], sh["extents"][1], sh["extents"][2]};

                                        spawn->shape.radius = sh.value("radius", 1.0f);
                                        spawn->shape.angle = sh.value("angle", 25.0f);
                                        spawn->shape.height = sh.value("height", 1.0f);
                                        spawn->shape.surfaceOnly = sh.value("surface_only", false);
                                    }
                                }

                                if (mods.contains("lifetime"))
                                {
                                    const auto &m = mods["lifetime"];
                                    auto *mod = emitter->addModule<LifetimeModule>();
                                    mod->setEnabled(m.value("enabled", true));
                                    mod->minLifetime = m.value("min", 1.0f);
                                    mod->maxLifetime = m.value("max", 2.0f);
                                }

                                if (mods.contains("initial_velocity"))
                                {
                                    const auto &m = mods["initial_velocity"];
                                    auto *mod = emitter->addModule<InitialVelocityModule>();
                                    mod->setEnabled(m.value("enabled", true));
                                    if (m.contains("base") && m["base"].is_array() && m["base"].size() == 3)
                                        mod->baseVelocity = {m["base"][0], m["base"][1], m["base"][2]};
                                    if (m.contains("randomness") && m["randomness"].is_array() && m["randomness"].size() == 3)
                                        mod->randomness = {m["randomness"][0], m["randomness"][1], m["randomness"][2]};
                                }

                                if (mods.contains("size_over_lifetime"))
                                {
                                    const auto &m = mods["size_over_lifetime"];
                                    auto *mod = emitter->addModule<SizeOverLifetimeModule>();
                                    mod->setEnabled(m.value("enabled", true));
                                    if (m.contains("base_size") && m["base_size"].is_array() && m["base_size"].size() == 2)
                                        mod->baseSize = {m["base_size"][0], m["base_size"][1]};
                                    if (m.contains("curve") && m["curve"].is_array())
                                    {
                                        mod->curve.clear();
                                        for (const auto &pt : m["curve"])
                                            mod->curve.push_back({pt.value("t", 0.0f), pt.value("v", 1.0f)});
                                    }
                                }

                                if (mods.contains("color_over_lifetime"))
                                {
                                    const auto &m = mods["color_over_lifetime"];
                                    auto *mod = emitter->addModule<ColorOverLifetimeModule>();
                                    mod->setEnabled(m.value("enabled", true));
                                    if (m.contains("gradient") && m["gradient"].is_array())
                                    {
                                        mod->gradient.clear();
                                        for (const auto &pt : m["gradient"])
                                        {
                                            GradientPoint gp;
                                            gp.time = pt.value("t", 0.0f);
                                            if (pt.contains("color") && pt["color"].is_array() && pt["color"].size() == 4)
                                                gp.color = {pt["color"][0], pt["color"][1], pt["color"][2], pt["color"][3]};
                                            mod->gradient.push_back(gp);
                                        }
                                    }
                                }

                                if (mods.contains("force"))
                                {
                                    const auto &m = mods["force"];
                                    auto *mod = emitter->addModule<ForceModule>();
                                    mod->setEnabled(m.value("enabled", true));
                                    if (m.contains("force") && m["force"].is_array() && m["force"].size() == 3)
                                        mod->force = {m["force"][0], m["force"][1], m["force"][2]};
                                    mod->drag = m.value("drag", 0.0f);
                                }

                                if (mods.contains("renderer"))
                                {
                                    const auto &m = mods["renderer"];
                                    auto *mod = emitter->addModule<RendererModule>();
                                    mod->setEnabled(m.value("enabled", true));
                                    mod->texturePath = resolveScenePath(m.value("texture_path", std::string{}));

                                    const std::string blendStr = m.value("blend_mode", "alpha_blend");
                                    if (blendStr == "additive")
                                        mod->blendMode = ParticleBlendMode::Additive;
                                    else if (blendStr == "premultiplied")
                                        mod->blendMode = ParticleBlendMode::Premultiplied;
                                    else
                                        mod->blendMode = ParticleBlendMode::AlphaBlend;

                                    const std::string faceStr = m.value("facing_mode", "camera_facing");
                                    if (faceStr == "velocity_aligned")
                                        mod->facingMode = ParticleFacingMode::VelocityAligned;
                                    else if (faceStr == "world_up")
                                        mod->facingMode = ParticleFacingMode::WorldUp;
                                    else
                                        mod->facingMode = ParticleFacingMode::CameraFacing;

                                    mod->castShadows = m.value("cast_shadows", false);
                                    mod->softParticles = m.value("soft_particles", false);
                                    mod->softParticleRange = m.value("soft_particle_range", 1.0f);
                                }
                            }
                        }
                    }

                    auto *psComp = gameObject->addComponent<ParticleSystemComponent>();
                    psComp->playOnStart = componentJson.value("play_on_start", true);
                    psComp->setParticleSystem(ps);
                }
            }
        }

        reportStatus("Resolving entity hierarchy...");

        for (const auto &[child, parentId] : pendingParents)
        {
            auto it = entitiesById.find(parentId);
            if (it == entitiesById.end())
            {
                VX_ENGINE_WARNING_STREAM("Parent entity with id " << parentId << " was not found while loading scene.\n");
                continue;
            }

            if (!child->setParent(it->second))
                VX_ENGINE_WARNING_STREAM("Failed to set parent for entity '" << child->getName() << "' while loading scene.\n");
        }

        reportStatus("Finalizing light transforms...");

        for (const auto &[entity, direction] : pendingLightDirections)
        {
            if (!entity)
                continue;

            if (auto *transform = entity->getComponent<Transform3DComponent>())
                transform->setWorldRotation(worldRotationFromForward(direction));

            if (auto *lightComponent = entity->getComponent<LightComponent>())
                lightComponent->syncFromOwnerTransform();
        }

        reportStatus("Finalizing animator states...");

        for (const auto &state : pendingAnimatorStates)
        {
            if (!state.entity)
                continue;
            auto *anim = state.entity->getComponent<AnimatorComponent>();
            if (!anim)
                continue;
            anim->setAnimationSpeed(state.speed);
            anim->setAnimationLooped(state.looped);
            anim->setAnimationPaused(state.paused);
            if (state.selectedAnim >= 0)
                anim->setSelectedAnimationIndex(state.selectedAnim);
        }
    }

    reportStatus("Finalizing scene...");

    file.close();

    reportStatus("Scene loaded");

    return true;
}

std::vector<std::shared_ptr<BaseLight>> Scene::getLights()
{
    std::vector<std::shared_ptr<BaseLight>> lights;

    for (const auto &entity : m_entities)
    {
        if (!entity || !entity->isEnabled())
            continue;

        if (auto *lightComponent = entity->getComponent<LightComponent>())
        {
            lightComponent->syncFromOwnerTransform();
            lights.push_back(lightComponent->getLight());
        }
    }

    return lights;
}

void Scene::saveSceneToFile(const std::string &filePath)
{
    nlohmann::json json;

    json["name"] = m_name.empty() ? std::filesystem::path(filePath).stem().string() : m_name;

    const std::filesystem::path sceneDirectory = std::filesystem::path(filePath).parent_path();

    auto toRelativePath = [&](const std::string &absolutePath) -> std::string
    {
        if (absolutePath.empty())
            return {};
        std::filesystem::path p = std::filesystem::path(absolutePath).lexically_normal();
        std::error_code ec;
        if (p.is_absolute() && !sceneDirectory.empty())
        {
            auto rel = std::filesystem::relative(p, sceneDirectory, ec);
            if (!ec && !rel.empty())
                p = rel.lexically_normal();
        }
        return p.string();
    };

    if (!m_skyboxHDRPath.empty())
        json["environment"] = {{"skybox_hdr", toRelativePath(m_skyboxHDRPath)}};

    {
        const auto &settings = RenderQualitySettings::getInstance();
        json["render_settings"] = {
            {"shadow_quality", settings.getShadowResolution()},
            {"shadow_cascade_count", settings.getShadowCascadeCount()},
            {"shadow_max_distance", settings.shadowMaxDistance},
            {"anti_aliasing_mode", static_cast<int>(settings.getAntiAliasingMode())},
            {"msaa_mode", static_cast<int>(settings.msaaMode)},
            {"anisotropy_mode", static_cast<int>(settings.anisotropyMode)},
            {"enable_post_processing", settings.enablePostProcessing},
            {"enable_vsync", settings.enableVSync},
            {"enable_ssao", settings.enableSSAO},
            {"ssao_radius", settings.ssaoRadius},
            {"ssao_bias", settings.ssaoBias},
            {"ssao_strength", settings.ssaoStrength},
            {"ssao_samples", settings.ssaoSamples},
            {"enable_gtao", settings.enableGTAO},
            {"gtao_directions", settings.gtaoDirections},
            {"gtao_steps", settings.gtaoSteps},
            {"use_bent_normals", settings.useBentNormals},
            {"enable_anisotropy", settings.enableAnisotropy},
            {"anisotropy_strength", settings.anisotropyStrength},
            {"anisotropy_rotation", settings.anisotropyRotation},
            {"shadow_ambient_strength", settings.shadowAmbientStrength},
            {"enable_shadow_occlusion_culling", settings.enableShadowOcclusionCulling},
            {"enable_occlusion_culling", settings.enableOcclusionCulling},
            {"occlusion_probe_interval", settings.occlusionProbeInterval},
            {"occlusion_visible_requery_interval", settings.occlusionVisibleRequeryInterval},
            {"occlusion_occluded_confirmation_queries", settings.occlusionOccludedConfirmationQueries},
            {"occlusion_max_instances_per_batch", settings.occlusionMaxInstancesPerBatch},
            {"occlusion_fast_motion_probe_interval", settings.occlusionFastMotionProbeInterval},
            {"occlusion_fast_motion_visible_requery_interval", settings.occlusionFastMotionVisibleRequeryInterval},
            {"occlusion_fast_motion_stale_reveal_frames", settings.occlusionFastMotionStaleRevealFrames},
            {"occlusion_fast_motion_translation_threshold", settings.occlusionFastMotionTranslationThreshold},
            {"occlusion_fast_motion_forward_dot_threshold", settings.occlusionFastMotionForwardDotThreshold},
            {"shadow_occlusion_visibility_grace_frames", settings.shadowOcclusionVisibilityGraceFrames},
            {"enable_lut_grading", settings.enableLUTGrading},
            {"lut_grading_path", toRelativePath(settings.lutGradingPath)},
            {"lut_grading_strength", settings.lutGradingStrength},
            {"enable_bloom", settings.enableBloom},
            {"enable_ssr", settings.enableSSR},
            {"enable_ibl", settings.enableIBL},
            {"ibl_diffuse_intensity", settings.iblDiffuseIntensity},
            {"ibl_specular_intensity", settings.iblSpecularIntensity},
            {"render_scale", settings.renderScale}};
    }

    const auto &objects = getEntities();

    for (const auto &object : objects)
    {
        nlohmann::json objectJson;

        objectJson["name"] = object->getName();
        objectJson["id"] = object->getId();
        objectJson["enabled"] = object->isEnabled();

        if (const auto *parent = object->getParent())
            objectJson["parent_id"] = parent->getId();

        if (const auto &transformation = object->getComponent<Transform3DComponent>())
        {
            const auto pos = transformation->getPosition();
            const auto scl = transformation->getScale();
            const auto rot = transformation->getEulerDegrees();
            objectJson["position"] = {pos.x, pos.y, pos.z};
            objectJson["scale"] = {scl.x, scl.y, scl.z};
            objectJson["rotation"] = {rot.x, rot.y, rot.z};
        }

        const auto &tags = object->getTags();
        if (!tags.empty())
        {
            nlohmann::json tagsJson = nlohmann::json::array();
            for (const auto &tag : tags)
                tagsJson.push_back(tag);
            objectJson["tags"] = tagsJson;
        }

        nlohmann::json componentsJson = nlohmann::json::array();

        if (const auto *sm = object->getComponent<StaticMeshComponent>())
        {
            nlohmann::json j;
            j["type"] = "static_mesh";

            const std::string &assetPath = sm->getAssetPath();
            if (assetPath.empty())
            {
                j["is_primitive"] = true;
                j["primitive_type"] = sm->getMeshes().empty() ? "Cube" : sm->getMeshes()[0].name;
            }
            else
            {
                j["is_primitive"] = false;
                j["asset_path"] = toRelativePath(assetPath);
            }

            nlohmann::json overridesJson = nlohmann::json::array();
            for (size_t slot = 0; slot < sm->getMaterialSlotCount(); ++slot)
            {
                const std::string &matPath = sm->getMaterialOverridePath(slot);
                if (!matPath.empty())
                    overridesJson.push_back({{"slot", slot}, {"path", toRelativePath(matPath)}});
            }
            j["material_overrides"] = overridesJson;

            componentsJson.push_back(j);
        }

        if (const auto *skm = object->getComponent<SkeletalMeshComponent>())
        {
            nlohmann::json j;
            j["type"] = "skeletal_mesh";
            j["asset_path"] = toRelativePath(skm->getAssetPath());

            nlohmann::json overridesJson = nlohmann::json::array();
            for (size_t slot = 0; slot < skm->getMaterialSlotCount(); ++slot)
            {
                const std::string &matPath = skm->getMaterialOverridePath(slot);
                if (!matPath.empty())
                    overridesJson.push_back({{"slot", slot}, {"path", toRelativePath(matPath)}});
            }
            j["material_overrides"] = overridesJson;

            componentsJson.push_back(j);
        }

        if (const auto *anim = object->getComponent<AnimatorComponent>())
        {
            nlohmann::json j;
            j["type"] = "animator";
            j["selected_animation"] = anim->getSelectedAnimationIndex();
            j["speed"] = anim->getAnimationSpeed();
            j["looped"] = anim->isAnimationLooped();
            j["paused"] = anim->isAnimationPaused();
            componentsJson.push_back(j);
        }

        if (const auto *cameraComponent = object->getComponent<CameraComponent>())
        {
            const auto camera = cameraComponent->getCamera();
            if (camera)
            {
                nlohmann::json j;
                j["type"] = "camera";
                j["yaw"] = camera->getYaw();
                j["pitch"] = camera->getPitch();
                j["fov"] = camera->getFOV();
                j["aspect"] = camera->getAspect();
                const glm::vec3 positionOffset = cameraComponent->getPositionOffset();
                j["position_offset"] = {positionOffset.x, positionOffset.y, positionOffset.z};

                componentsJson.push_back(j);
            }
        }

        if (auto *lightComponent = object->getComponent<LightComponent>())
        {
            nlohmann::json lightJson;
            const auto &light = lightComponent->getLight();
            lightJson["type"] = "light";

            std::string stringLightType;
            switch (lightComponent->getLightType())
            {
            case LightComponent::LightType::DIRECTIONAL:
                stringLightType = "directional";
                break;
            case LightComponent::LightType::SPOT:
                stringLightType = "spot";
                break;
            case LightComponent::LightType::POINT:
                stringLightType = "point";
                break;
            default:
                stringLightType = "undefined";
                break;
            }

            lightJson["light_type"] = stringLightType;
            lightJson["color"] = {light->color.x, light->color.y, light->color.z};
            lightJson["strength"] = light->strength;
            lightJson["casts_shadows"] = light->castsShadows;

            if (lightComponent->getLightType() == LightComponent::LightType::DIRECTIONAL)
            {
                if (auto *dl = dynamic_cast<DirectionalLight *>(light.get()))
                    lightJson["sky_light_enabled"] = dl->skyLightEnabled;
            }
            else if (lightComponent->getLightType() == LightComponent::LightType::POINT)
            {
                if (auto *pl = dynamic_cast<PointLight *>(light.get()))
                {
                    lightJson["radius"] = pl->radius;
                    lightJson["falloff"] = pl->falloff;
                }
            }
            else if (lightComponent->getLightType() == LightComponent::LightType::SPOT)
            {
                if (auto *sl = dynamic_cast<SpotLight *>(light.get()))
                {
                    lightJson["inner_angle"] = sl->innerAngle;
                    lightJson["outer_angle"] = sl->outerAngle;
                    lightJson["range"] = sl->range;
                }
            }

            componentsJson.push_back(lightJson);
        }

        if (const auto *rb = object->getComponent<RigidBodyComponent>())
        {
            nlohmann::json j;
            j["type"] = "rigid_body";
            j["is_kinematic"] = rb->isKinematic();
            componentsJson.push_back(j);
        }

        if (const auto *collision = object->getComponent<CollisionComponent>())
        {
            nlohmann::json j;
            j["type"] = "collision";
            if (collision->getShapeType() == CollisionComponent::ShapeType::CAPSULE)
            {
                j["collision_type"] = "capsule";
                j["radius"] = collision->getCapsuleRadius();
                j["half_height"] = collision->getCapsuleHalfHeight();
            }
            else
            {
                j["collision_type"] = "box";
                const auto he = collision->getBoxHalfExtents();
                j["half_extents"] = {he.x, he.y, he.z};
            }
            componentsJson.push_back(j);
        }

        if (const auto *characterMovement = object->getComponent<CharacterMovementComponent>())
        {
            nlohmann::json j;
            j["type"] = "character_movement";
            j["radius"] = characterMovement->getCapsuleRadius();
            j["height"] = characterMovement->getCapsuleHeight();
            j["step_offset"] = characterMovement->getStepOffset();
            j["contact_offset"] = characterMovement->getContactOffset();
            j["slope_limit_degrees"] = characterMovement->getSlopeLimitDegrees();
            componentsJson.push_back(j);
        }

        for (const auto *audio : object->getComponents<AudioComponent>())
        {
            nlohmann::json j;
            j["type"] = "audio";
            j["asset_path"] = toRelativePath(audio->getAssetPath());
            j["volume"] = audio->getVolume();
            j["pitch"] = audio->getPitch();
            j["loop"] = audio->isLooping();
            j["play_on_start"] = audio->isPlayOnStart();
            j["muted"] = audio->isMuted();
            j["spatial"] = audio->isSpatial();
            j["min_distance"] = audio->getMinDistance();
            j["max_distance"] = audio->getMaxDistance();
            j["audio_type"] = (audio->getAudioType() == AudioComponent::AudioType::Music) ? "music" : "sound";
            componentsJson.push_back(j);
        }

        for (auto *script : object->getComponents<ScriptComponent>())
        {
            if (script->getScriptName().empty())
                continue;

            script->syncSerializedVariablesFromScript();
            nlohmann::json j;
            j["type"] = "script";
            j["name"] = script->getScriptName();

            const auto &serializedVariables = script->getSerializedVariables();
            if (!serializedVariables.empty())
            {
                nlohmann::json variablesJson = nlohmann::json::object();
                for (const auto &[variableName, variable] : serializedVariables)
                    variablesJson[variableName] = scriptVariableToJson(variable);

                j["variables"] = std::move(variablesJson);
            }

            componentsJson.push_back(j);
        }

        for (const auto *psComp : object->getComponents<ParticleSystemComponent>())
        {
            const ParticleSystem *ps = psComp->getParticleSystem();
            if (!ps)
                continue;

            nlohmann::json j;
            j["type"] = "particle_system";
            j["play_on_start"] = psComp->playOnStart;

            nlohmann::json sysJson;
            sysJson["name"] = ps->name;

            nlohmann::json emittersJson = nlohmann::json::array();
            for (const auto &emitter : ps->getEmitters())
            {
                nlohmann::json emJson;
                emJson["name"] = emitter->name;
                emJson["enabled"] = emitter->enabled;

                nlohmann::json modulesJson;

                if (auto *spawn = emitter->getModule<SpawnModule>())
                {
                    nlohmann::json m;
                    m["enabled"] = spawn->isEnabled();
                    m["spawn_rate"] = spawn->spawnRate;
                    m["burst_count"] = spawn->burstCount;
                    m["loop"] = spawn->loop;
                    m["duration"] = spawn->duration;

                    std::string shapeStr;
                    switch (spawn->shape.shape)
                    {
                    case EmitterShape::Sphere:
                        shapeStr = "sphere";
                        break;
                    case EmitterShape::Box:
                        shapeStr = "box";
                        break;
                    case EmitterShape::Cone:
                        shapeStr = "cone";
                        break;
                    case EmitterShape::Cylinder:
                        shapeStr = "cylinder";
                        break;
                    default:
                        shapeStr = "point";
                        break;
                    }
                    nlohmann::json shapeJson;
                    shapeJson["type"] = shapeStr;
                    shapeJson["extents"] = {spawn->shape.extents.x, spawn->shape.extents.y, spawn->shape.extents.z};
                    shapeJson["radius"] = spawn->shape.radius;
                    shapeJson["angle"] = spawn->shape.angle;
                    shapeJson["height"] = spawn->shape.height;
                    shapeJson["surface_only"] = spawn->shape.surfaceOnly;
                    m["shape"] = shapeJson;

                    modulesJson["spawn"] = m;
                }

                if (auto *lifetime = emitter->getModule<LifetimeModule>())
                {
                    nlohmann::json m;
                    m["enabled"] = lifetime->isEnabled();
                    m["min"] = lifetime->minLifetime;
                    m["max"] = lifetime->maxLifetime;
                    modulesJson["lifetime"] = m;
                }

                if (auto *vel = emitter->getModule<InitialVelocityModule>())
                {
                    nlohmann::json m;
                    m["enabled"] = vel->isEnabled();
                    m["base"] = {vel->baseVelocity.x, vel->baseVelocity.y, vel->baseVelocity.z};
                    m["randomness"] = {vel->randomness.x, vel->randomness.y, vel->randomness.z};
                    modulesJson["initial_velocity"] = m;
                }

                if (auto *size = emitter->getModule<SizeOverLifetimeModule>())
                {
                    nlohmann::json m;
                    m["enabled"] = size->isEnabled();
                    m["base_size"] = {size->baseSize.x, size->baseSize.y};

                    nlohmann::json curveJson = nlohmann::json::array();
                    for (const auto &pt : size->curve)
                        curveJson.push_back({{"t", pt.time}, {"v", pt.value}});
                    m["curve"] = curveJson;

                    modulesJson["size_over_lifetime"] = m;
                }

                if (auto *col = emitter->getModule<ColorOverLifetimeModule>())
                {
                    nlohmann::json m;
                    m["enabled"] = col->isEnabled();

                    nlohmann::json gradJson = nlohmann::json::array();
                    for (const auto &pt : col->gradient)
                        gradJson.push_back({{"t", pt.time}, {"color", {pt.color.r, pt.color.g, pt.color.b, pt.color.a}}});
                    m["gradient"] = gradJson;

                    modulesJson["color_over_lifetime"] = m;
                }

                if (auto *force = emitter->getModule<ForceModule>())
                {
                    nlohmann::json m;
                    m["enabled"] = force->isEnabled();
                    m["force"] = {force->force.x, force->force.y, force->force.z};
                    m["drag"] = force->drag;
                    modulesJson["force"] = m;
                }

                if (auto *renderer = emitter->getModule<RendererModule>())
                {
                    nlohmann::json m;
                    m["enabled"] = renderer->isEnabled();
                    m["texture_path"] = toRelativePath(renderer->texturePath);

                    std::string blendStr;
                    switch (renderer->blendMode)
                    {
                    case ParticleBlendMode::Additive:
                        blendStr = "additive";
                        break;
                    case ParticleBlendMode::Premultiplied:
                        blendStr = "premultiplied";
                        break;
                    default:
                        blendStr = "alpha_blend";
                        break;
                    }
                    m["blend_mode"] = blendStr;

                    std::string faceStr;
                    switch (renderer->facingMode)
                    {
                    case ParticleFacingMode::VelocityAligned:
                        faceStr = "velocity_aligned";
                        break;
                    case ParticleFacingMode::WorldUp:
                        faceStr = "world_up";
                        break;
                    default:
                        faceStr = "camera_facing";
                        break;
                    }
                    m["facing_mode"] = faceStr;
                    m["cast_shadows"] = renderer->castShadows;
                    m["soft_particles"] = renderer->softParticles;
                    m["soft_particle_range"] = renderer->softParticleRange;

                    modulesJson["renderer"] = m;
                }

                emJson["modules"] = modulesJson;
                emittersJson.push_back(emJson);
            }

            sysJson["emitters"] = emittersJson;
            j["system"] = sysJson;

            componentsJson.push_back(j);
        }

        objectJson["components"] = componentsJson;

        json["game_objects"].push_back(objectJson);
    }

    {
        nlohmann::json uiObjectsJson = nlohmann::json::array();

        for (const auto &text : m_uiTexts)
        {
            if (!text)
                continue;

            const auto position = text->getPosition();
            const auto color = text->getColor();

            nlohmann::json textJson;
            textJson["type"] = "text";
            textJson["enabled"] = text->isEnabled();
            textJson["text"] = text->getText();
            textJson["position"] = {position.x, position.y};
            textJson["scale"] = text->getScale();
            textJson["rotation"] = text->getRotation();
            textJson["color"] = {color.x, color.y, color.z, color.w};

            if (const auto *font = text->getFont(); font && !font->getFontPath().empty())
                textJson["font_path"] = toRelativePath(font->getFontPath());

            uiObjectsJson.push_back(std::move(textJson));
        }

        for (const auto &button : m_uiButtons)
        {
            if (!button)
                continue;

            const auto position = button->getPosition();
            const auto size = button->getSize();
            const auto backgroundColor = button->getBackgroundColor();
            const auto hoverColor = button->getHoverColor();
            const auto borderColor = button->getBorderColor();
            const auto labelColor = button->getLabelColor();

            nlohmann::json buttonJson;
            buttonJson["type"] = "button";
            buttonJson["enabled"] = button->isEnabled();
            buttonJson["position"] = {position.x, position.y};
            buttonJson["size"] = {size.x, size.y};
            buttonJson["background_color"] = {backgroundColor.x, backgroundColor.y, backgroundColor.z, backgroundColor.w};
            buttonJson["hover_color"] = {hoverColor.x, hoverColor.y, hoverColor.z, hoverColor.w};
            buttonJson["border_color"] = {borderColor.x, borderColor.y, borderColor.z, borderColor.w};
            buttonJson["border_width"] = button->getBorderWidth();
            buttonJson["label"] = button->getLabel();
            buttonJson["label_color"] = {labelColor.x, labelColor.y, labelColor.z, labelColor.w};
            buttonJson["label_scale"] = button->getLabelScale();
            buttonJson["rotation"] = button->getRotation();

            if (const auto *font = button->getFont(); font && !font->getFontPath().empty())
                buttonJson["font_path"] = toRelativePath(font->getFontPath());

            uiObjectsJson.push_back(std::move(buttonJson));
        }

        for (const auto &billboard : m_billboards)
        {
            if (!billboard)
                continue;

            const auto worldPosition = billboard->getWorldPosition();
            const auto color = billboard->getColor();

            nlohmann::json billboardJson;
            billboardJson["type"] = "billboard";
            billboardJson["enabled"] = billboard->isEnabled();
            billboardJson["world_position"] = {worldPosition.x, worldPosition.y, worldPosition.z};
            billboardJson["size"] = billboard->getSize();
            billboardJson["rotation"] = billboard->getRotation();
            billboardJson["color"] = {color.x, color.y, color.z, color.w};

            if (!billboard->getTexturePath().empty())
                billboardJson["texture_path"] = toRelativePath(billboard->getTexturePath());

            uiObjectsJson.push_back(std::move(billboardJson));
        }

        json["ui_objects"] = std::move(uiObjectsJson);
    }

    std::ofstream file(filePath);

    if (file.is_open())
    {
        file << std::setw(4) << json << std::endl;
        file.close();
        VX_ENGINE_INFO_STREAM("Saved scene in " << filePath << '\n');
    }
    else
        VX_ENGINE_ERROR_STREAM("Failed to open file to save game objects: " << filePath << std::endl);
}

bool Scene::destroyEntity(Entity *entity)
{
    if (!entity)
        return false;

    auto exists = std::find_if(m_entities.begin(), m_entities.end(), [entity](const std::shared_ptr<Entity> &en)
                               { return en.get() == entity; });
    if (exists == m_entities.end())
        return false;

    std::unordered_set<Entity *> entitiesToDestroy;
    std::vector<Entity *> stack{entity};

    while (!stack.empty())
    {
        Entity *current = stack.back();
        stack.pop_back();

        if (!current || entitiesToDestroy.contains(current))
            continue;

        entitiesToDestroy.insert(current);

        for (auto *child : current->getChildren())
            stack.push_back(child);
    }

    for (auto *current : entitiesToDestroy)
        if (current)
            current->clearParent();

    m_entities.erase(
        std::remove_if(m_entities.begin(), m_entities.end(), [&entitiesToDestroy](const std::shared_ptr<Entity> &en)
                       { return entitiesToDestroy.contains(en.get()); }),
        m_entities.end());

    return true;
}

void Scene::update(float deltaTime)
{
    // Scripts can spawn/destroy entities during update.
    // Iterate by index and copy shared_ptr to avoid iterator/reference invalidation.
    for (size_t index = 0; index < m_entities.size(); ++index)
    {
        auto entity = m_entities[index];
        if (entity && entity->isEnabled())
            entity->update(deltaTime);
    }

    m_physicsScene.update(deltaTime);

    for (size_t index = 0; index < m_entities.size(); ++index)
    {
        auto entity = m_entities[index];
        if (!entity)
            continue;

        if (!entity->isEnabled())
            continue;

        if (auto *rigidBodyComponent = entity->getComponent<RigidBodyComponent>())
            rigidBodyComponent->syncFromPhysics();
    }
}

void Scene::fixedUpdate(float fixedDelta)
{
    for (size_t index = 0; index < m_entities.size(); ++index)
    {
        auto entity = m_entities[index];
        if (entity && entity->isEnabled())
            entity->fixedUpdate(fixedDelta);
    }
}

ELIX_NESTED_NAMESPACE_END
