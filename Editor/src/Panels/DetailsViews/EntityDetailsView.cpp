#include "Editor/Panels/DetailsViews/EntityDetailsView.hpp"

#include "Editor/Editor.hpp"

#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/PluginSystem/ComponentRegistry.hpp"

#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Components/AudioComponent.hpp"
#include "Engine/Components/ReflectionProbeComponent.hpp"
#include "Engine/Components/DecalComponent.hpp"
#include "Engine/Components/SpriteComponent.hpp"
#include "Engine/Components/CameraComponent.hpp"
#include "Core/VulkanContext.hpp"
#include "Engine/Components/CharacterMovementComponent.hpp"
#include "Engine/Components/CollisionComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/ParticleSystemComponent.hpp"
#include "Engine/Components/RagdollComponent.hpp"
#include "Engine/Components/RigidBodyComponent.hpp"
#include "Engine/Components/ScriptComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Skeleton.hpp"
#include "Engine/Particles/Modules/ColorOverLifetimeModule.hpp"
#include "Engine/Particles/Modules/ForceModule.hpp"
#include "Engine/Particles/Modules/InitialVelocityModule.hpp"
#include "Engine/Particles/Modules/LifetimeModule.hpp"
#include "Engine/Particles/Modules/RendererModule.hpp"
#include "Engine/Particles/Modules/SizeOverLifetimeModule.hpp"
#include "Engine/Particles/Modules/SpawnModule.hpp"
#include "Engine/Particles/Modules/VelocityOverLifetimeModule.hpp"
#include "Engine/Particles/Modules/RotationOverLifetimeModule.hpp"
#include "Engine/Particles/Modules/TurbulenceModule.hpp"
#include "Engine/Primitives.hpp"

#include "nlohmann/json.hpp"
#include <imgui.h>
#include <glm/common.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace
{
    struct ComponentClipboardEntry
    {
        bool hasData{false};
        std::string typeKey;
        std::string label;
        nlohmann::json payload;
    };

    ComponentClipboardEntry g_componentClipboard;

    bool drawVec3Control(const char *id, glm::vec3 &v, float resetValue = 0.0f, float speed = 0.01f)
    {
        bool changed = false;

        const float lineHeight = ImGui::GetTextLineHeight();
        const ImVec2 btnSize = {lineHeight + 6.0f, lineHeight + 4.0f};
        const float totalBtnW = 3.0f * btnSize.x + 2.0f * 2.0f;
        const float fieldW = (ImGui::CalcItemWidth() - totalBtnW - 2.0f * 4.0f) / 3.0f;

        ImGui::PushID(id);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ImGui::GetStyle().ItemSpacing.y));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.14f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.24f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.08f, 0.08f, 1.0f));
        if (ImGui::Button("X", btnSize))
        {
            v.x = resetValue;
            changed = true;
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0, 1);
        ImGui::SetNextItemWidth(fieldW);
        changed |= ImGui::DragFloat("##X", &v.x, speed, 0.0f, 0.0f, "%.3f");
        ImGui::SameLine(0, 4);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.58f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.74f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.42f, 0.10f, 1.0f));
        if (ImGui::Button("Y", btnSize))
        {
            v.y = resetValue;
            changed = true;
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0, 1);
        ImGui::SetNextItemWidth(fieldW);
        changed |= ImGui::DragFloat("##Y", &v.y, speed, 0.0f, 0.0f, "%.3f");
        ImGui::SameLine(0, 4);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.26f, 0.72f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.36f, 0.88f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.16f, 0.55f, 1.0f));
        if (ImGui::Button("Z", btnSize))
        {
            v.z = resetValue;
            changed = true;
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0, 1);
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::DragFloat("##Z", &v.z, speed, 0.0f, 0.0f, "%.3f");

        ImGui::PopStyleVar();
        ImGui::PopID();
        return changed;
    }

    std::string toLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    bool isParticleTextureAssetPath(const std::string &path)
    {
        if (path.empty())
            return false;

        const std::string lowerPath = toLowerCopy(path);
        if (lowerPath.size() >= 14u && lowerPath.rfind(".tex.elixasset") == (lowerPath.size() - 14u))
            return true;

        const std::string extension = toLowerCopy(std::filesystem::path(path).extension().string());
        return extension == ".png" ||
               extension == ".jpg" ||
               extension == ".jpeg" ||
               extension == ".tga" ||
               extension == ".bmp" ||
               extension == ".dds" ||
               extension == ".ktx" ||
               extension == ".ktx2" ||
               extension == ".hdr" ||
               extension == ".exr";
    }

    std::string normalizePathAgainstProjectRoot(const std::string &rawPath, const std::filesystem::path &projectRoot)
    {
        if (rawPath.empty())
            return {};

        std::filesystem::path resolvedPath(rawPath);
        if (resolvedPath.is_relative() && !projectRoot.empty())
            resolvedPath = projectRoot / resolvedPath;

        std::error_code errorCode;
        resolvedPath = std::filesystem::absolute(resolvedPath, errorCode);
        if (errorCode)
            return std::filesystem::path(rawPath).lexically_normal().string();

        return resolvedPath.lexically_normal().string();
    }

    std::vector<std::string> collectProjectParticleTextureCandidates(const std::filesystem::path &projectRoot)
    {
        std::vector<std::string> result;
        std::error_code directoryError;
        if (projectRoot.empty() || !std::filesystem::is_directory(projectRoot, directoryError) || directoryError)
            return result;

        std::error_code iteratorError;
        for (std::filesystem::recursive_directory_iterator iterator(projectRoot, iteratorError), end; iterator != end; iterator.increment(iteratorError))
        {
            if (iteratorError)
            {
                iteratorError.clear();
                continue;
            }

            if (!iterator->is_regular_file())
                continue;

            const std::filesystem::path candidatePath = iterator->path().lexically_normal();
            if (!isParticleTextureAssetPath(candidatePath.string()))
                continue;

            result.push_back(candidatePath.string());
        }

        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    std::string formatTexturePathForDisplay(const std::string &path, const std::filesystem::path &projectRoot)
    {
        if (path.empty())
            return "<None>";

        std::filesystem::path candidate(path);
        if (candidate.is_absolute() && !projectRoot.empty())
        {
            std::error_code errorCode;
            const std::filesystem::path relative = std::filesystem::relative(candidate, projectRoot, errorCode);
            if (!errorCode && !relative.empty())
                return relative.lexically_normal().string();
        }

        return candidate.lexically_normal().string();
    }

    bool drawStringInput(const char *label, std::string &value)
    {
        std::array<char, 256> buffer{};
        std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1);
        if (!ImGui::InputText(label, buffer.data(), buffer.size()))
            return false;

        value = buffer.data();
        return true;
    }

    bool drawBoneCombo(const char *label,
                       const elix::engine::Skeleton *skeleton,
                       std::string &boneName)
    {
        const char *preview = boneName.empty() ? "<None>" : boneName.c_str();
        bool changed = false;

        if (!ImGui::BeginCombo(label, preview))
            return false;

        if (ImGui::Selectable("<None>", boneName.empty()))
        {
            boneName.clear();
            changed = true;
        }

        if (skeleton)
        {
            for (size_t boneIndex = 0; boneIndex < skeleton->getBonesCount(); ++boneIndex)
            {
                const auto *bone = skeleton->getBone(static_cast<int>(boneIndex));
                if (!bone)
                    continue;

                const bool isSelected = boneName == bone->name;
                if (ImGui::Selectable(bone->name.c_str(), isSelected))
                {
                    boneName = bone->name;
                    changed = true;
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
        return changed;
    }

    physx::PxTransform makePxTransformFromEntity(elix::engine::Entity *entity)
    {
        if (!entity)
            return physx::PxTransform(physx::PxIdentity);

        auto *transformComponent = entity->getComponent<elix::engine::Transform3DComponent>();
        if (!transformComponent)
            return physx::PxTransform(physx::PxIdentity);

        const glm::vec3 worldPosition = transformComponent->getWorldPosition();
        const glm::quat worldRotation = transformComponent->getWorldRotation();
        return physx::PxTransform(
            physx::PxVec3(worldPosition.x, worldPosition.y, worldPosition.z),
            physx::PxQuat(worldRotation.x, worldRotation.y, worldRotation.z, worldRotation.w));
    }

    void destroyCollisionComponent(elix::engine::Scene *scene, elix::engine::Entity *entity, elix::engine::CollisionComponent *collisionComponent)
    {
        if (!entity || !collisionComponent)
            return;

        if (auto *rigidBodyComponent = entity->getComponent<elix::engine::RigidBodyComponent>())
        {
            if (auto *shape = collisionComponent->getShape())
                rigidBodyComponent->getRigidActor()->detachShape(*shape);
        }

        if (auto *staticActor = collisionComponent->getActor())
        {
            if (scene)
                scene->getPhysicsScene().removeActor(*staticActor, true, true);
        }

        if (auto *shape = collisionComponent->getShape())
            shape->release();

        entity->removeComponent<elix::engine::CollisionComponent>();
    }

    void destroyRigidBodyComponent(elix::engine::Scene *scene, elix::engine::Entity *entity, elix::engine::RigidBodyComponent *rigidBodyComponent)
    {
        if (!entity || !rigidBodyComponent)
            return;

        if (auto *collisionComponent = entity->getComponent<elix::engine::CollisionComponent>())
            destroyCollisionComponent(scene, entity, collisionComponent);

        if (auto *rigidActor = rigidBodyComponent->getRigidActor())
        {
            if (scene)
                scene->getPhysicsScene().removeActor(*rigidActor, true, true);
        }

        entity->removeComponent<elix::engine::RigidBodyComponent>();
    }

    bool hasOtherDirectionalLight(const elix::engine::Scene *scene, const elix::engine::Entity *excludeEntity);

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
                outVariable.value = elix::engine::Script::EntityRef{};
                return true;
            }

            if (valueJson.is_number_unsigned())
            {
                outVariable.value = elix::engine::Script::EntityRef(valueJson.get<uint32_t>());
                return true;
            }

            if (valueJson.is_number_integer())
            {
                const int64_t signedId = valueJson.get<int64_t>();
                if (signedId < 0 || signedId > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
                    return false;

                outVariable.value = elix::engine::Script::EntityRef(static_cast<uint32_t>(signedId));
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
                else if constexpr (std::is_same_v<T, elix::engine::Script::EntityRef>)
                    jsonValue["value"] = value.isValid() ? nlohmann::json(value.id) : nlohmann::json(nullptr);
                else
                    jsonValue["value"] = value;
            },
            variable.value);

        return jsonValue;
    }

    std::string componentClipboardTypeKey(elix::engine::ECS *component)
    {
        if (!component)
            return {};

        if (auto *scriptComponent = dynamic_cast<elix::engine::ScriptComponent *>(component))
            return "script:" + scriptComponent->getScriptName();
        if (dynamic_cast<elix::engine::Transform3DComponent *>(component))
            return "transform3d";
        if (dynamic_cast<elix::engine::CameraComponent *>(component))
            return "camera";
        if (dynamic_cast<elix::engine::CharacterMovementComponent *>(component))
            return "character_movement";
        if (dynamic_cast<elix::engine::CollisionComponent *>(component))
            return "collision";
        if (dynamic_cast<elix::engine::LightComponent *>(component))
            return "light";
        return {};
    }

    std::optional<nlohmann::json> serializeComponentForClipboard(elix::engine::Entity *entity,
                                                                 elix::engine::ECS *component)
    {
        if (!entity || !component)
            return std::nullopt;

        if (auto *transform = dynamic_cast<elix::engine::Transform3DComponent *>(component))
        {
            const glm::vec3 position = transform->getPosition();
            const glm::vec3 scale = transform->getScale();
            const glm::vec3 rotation = transform->getEulerDegrees();
            return nlohmann::json{
                {"position", {position.x, position.y, position.z}},
                {"scale", {scale.x, scale.y, scale.z}},
                {"rotation", {rotation.x, rotation.y, rotation.z}}};
        }

        if (auto *cameraComponent = dynamic_cast<elix::engine::CameraComponent *>(component))
        {
            auto camera = cameraComponent->getCamera();
            if (!camera)
                return std::nullopt;

            const glm::vec3 offset = cameraComponent->getPositionOffset();
            return nlohmann::json{
                {"yaw", camera->getYaw()},
                {"pitch", camera->getPitch()},
                {"fov", camera->getFOV()},
                {"aspect", camera->getAspect()},
                {"position_offset", {offset.x, offset.y, offset.z}}};
        }

        if (auto *movement = dynamic_cast<elix::engine::CharacterMovementComponent *>(component))
        {
            return nlohmann::json{
                {"radius", movement->getCapsuleRadius()},
                {"height", movement->getCapsuleHeight()},
                {"center_offset_y", movement->getCapsuleCenterOffsetY()},
                {"step_offset", movement->getStepOffset()},
                {"contact_offset", movement->getContactOffset()},
                {"slope_limit_degrees", movement->getSlopeLimitDegrees()}};
        }

        if (auto *collision = dynamic_cast<elix::engine::CollisionComponent *>(component))
        {
            nlohmann::json payload;
            payload["shape_type"] = collision->getShapeType() == elix::engine::CollisionComponent::ShapeType::CAPSULE
                                        ? "capsule"
                                        : "box";
            payload["is_trigger"] = collision->isTrigger();
            if (collision->getShapeType() == elix::engine::CollisionComponent::ShapeType::CAPSULE)
            {
                payload["radius"] = collision->getCapsuleRadius();
                payload["half_height"] = collision->getCapsuleHalfHeight();
            }
            else
            {
                const glm::vec3 halfExtents = collision->getBoxHalfExtents();
                payload["half_extents"] = {halfExtents.x, halfExtents.y, halfExtents.z};
            }
            return payload;
        }

        if (auto *lightComponent = dynamic_cast<elix::engine::LightComponent *>(component))
        {
            auto light = lightComponent->getLight();
            if (!light)
                return std::nullopt;

            nlohmann::json payload;
            switch (lightComponent->getLightType())
            {
            case elix::engine::LightComponent::LightType::DIRECTIONAL:
                payload["light_type"] = "directional";
                if (auto *directionalLight = dynamic_cast<elix::engine::DirectionalLight *>(light.get()))
                    payload["sky_light_enabled"] = directionalLight->skyLightEnabled;
                break;
            case elix::engine::LightComponent::LightType::SPOT:
                payload["light_type"] = "spot";
                if (auto *spotLight = dynamic_cast<elix::engine::SpotLight *>(light.get()))
                {
                    payload["inner_angle"] = spotLight->innerAngle;
                    payload["outer_angle"] = spotLight->outerAngle;
                    payload["range"] = spotLight->range;
                }
                break;
            case elix::engine::LightComponent::LightType::POINT:
                payload["light_type"] = "point";
                if (auto *pointLight = dynamic_cast<elix::engine::PointLight *>(light.get()))
                {
                    payload["radius"] = pointLight->radius;
                    payload["falloff"] = pointLight->falloff;
                }
                break;
            default:
                return std::nullopt;
            }

            payload["color"] = {light->color.x, light->color.y, light->color.z};
            payload["strength"] = light->strength;
            payload["casts_shadows"] = light->castsShadows;
            return payload;
        }

        if (auto *scriptComponent = dynamic_cast<elix::engine::ScriptComponent *>(component))
        {
            if (scriptComponent->getScriptName().empty())
                return std::nullopt;

            scriptComponent->syncSerializedVariablesFromScript();
            nlohmann::json variablesJson = nlohmann::json::object();
            for (const auto &[variableName, variable] : scriptComponent->getSerializedVariables())
                variablesJson[variableName] = scriptVariableToJson(variable);

            return nlohmann::json{
                {"script_name", scriptComponent->getScriptName()},
                {"variables", std::move(variablesJson)}};
        }

        return std::nullopt;
    }

    bool applyComponentClipboardPayload(elix::engine::Scene *scene,
                                        elix::engine::Entity *entity,
                                        elix::engine::ECS *component,
                                        const nlohmann::json &payload,
                                        std::string &outError)
    {
        outError.clear();

        if (!entity || !component)
        {
            outError = "Invalid component target";
            return false;
        }

        if (auto *transform = dynamic_cast<elix::engine::Transform3DComponent *>(component))
        {
            if (payload.contains("position") && payload["position"].is_array() && payload["position"].size() == 3)
                transform->setPosition(glm::vec3(payload["position"][0], payload["position"][1], payload["position"][2]));
            if (payload.contains("scale") && payload["scale"].is_array() && payload["scale"].size() == 3)
                transform->setScale(glm::vec3(payload["scale"][0], payload["scale"][1], payload["scale"][2]));
            if (payload.contains("rotation") && payload["rotation"].is_array() && payload["rotation"].size() == 3)
                transform->setEulerDegrees(glm::vec3(payload["rotation"][0], payload["rotation"][1], payload["rotation"][2]));
            return true;
        }

        if (auto *cameraComponent = dynamic_cast<elix::engine::CameraComponent *>(component))
        {
            auto camera = cameraComponent->getCamera();
            if (!camera)
            {
                outError = "Camera is missing";
                return false;
            }

            camera->setYaw(payload.value("yaw", camera->getYaw()));
            camera->setPitch(payload.value("pitch", camera->getPitch()));
            camera->setFOV(payload.value("fov", camera->getFOV()));
            camera->setAspect(payload.value("aspect", camera->getAspect()));

            if (payload.contains("position_offset") && payload["position_offset"].is_array() && payload["position_offset"].size() == 3)
                cameraComponent->setPositionOffset(glm::vec3(payload["position_offset"][0],
                                                             payload["position_offset"][1],
                                                             payload["position_offset"][2]));
            return true;
        }

        if (auto *movement = dynamic_cast<elix::engine::CharacterMovementComponent *>(component))
        {
            movement->setCapsule(std::max(payload.value("radius", movement->getCapsuleRadius()), 0.05f),
                                 std::max(payload.value("height", movement->getCapsuleHeight()), 0.1f));
            movement->setCapsuleCenterOffsetY(payload.value("center_offset_y", movement->getCapsuleCenterOffsetY()));
            movement->setStepOffset(payload.value("step_offset", movement->getStepOffset()));
            movement->setContactOffset(payload.value("contact_offset", movement->getContactOffset()));
            movement->setSlopeLimitDegrees(payload.value("slope_limit_degrees", movement->getSlopeLimitDegrees()));
            return true;
        }

        if (auto *collision = dynamic_cast<elix::engine::CollisionComponent *>(component))
        {
            const std::string shapeType = payload.value("shape_type",
                                                        collision->getShapeType() == elix::engine::CollisionComponent::ShapeType::CAPSULE
                                                            ? std::string("capsule")
                                                            : std::string("box"));
            const bool targetCapsule = shapeType == "capsule";
            const bool currentCapsule = collision->getShapeType() == elix::engine::CollisionComponent::ShapeType::CAPSULE;
            if (targetCapsule != currentCapsule)
            {
                outError = "Shape type paste is only supported onto the same collision shape type";
                return false;
            }

            if (currentCapsule)
            {
                collision->setCapsuleDimensions(std::max(payload.value("radius", collision->getCapsuleRadius()), 0.01f),
                                                std::max(payload.value("half_height", collision->getCapsuleHalfHeight()), 0.0f));
            }
            else if (payload.contains("half_extents") && payload["half_extents"].is_array() && payload["half_extents"].size() == 3)
            {
                collision->setBoxHalfExtents(glm::vec3(payload["half_extents"][0],
                                                       payload["half_extents"][1],
                                                       payload["half_extents"][2]));
            }

            collision->setTrigger(payload.value("is_trigger", collision->isTrigger()));
            return true;
        }

        if (auto *lightComponent = dynamic_cast<elix::engine::LightComponent *>(component))
        {
            const std::string lightTypeString = payload.value("light_type", std::string{});
            elix::engine::LightComponent::LightType requestedType = lightComponent->getLightType();
            if (lightTypeString == "directional")
                requestedType = elix::engine::LightComponent::LightType::DIRECTIONAL;
            else if (lightTypeString == "spot")
                requestedType = elix::engine::LightComponent::LightType::SPOT;
            else if (lightTypeString == "point")
                requestedType = elix::engine::LightComponent::LightType::POINT;

            if (requestedType == elix::engine::LightComponent::LightType::DIRECTIONAL &&
                lightComponent->getLightType() != elix::engine::LightComponent::LightType::DIRECTIONAL &&
                hasOtherDirectionalLight(scene, entity))
            {
                outError = "Only one directional light is allowed in a scene";
                return false;
            }

            if (requestedType != lightComponent->getLightType())
                lightComponent->changeLightType(requestedType);

            auto light = lightComponent->getLight();
            if (!light)
            {
                outError = "Light instance is missing";
                return false;
            }

            if (payload.contains("color") && payload["color"].is_array() && payload["color"].size() == 3)
                light->color = glm::vec3(payload["color"][0], payload["color"][1], payload["color"][2]);
            light->strength = payload.value("strength", light->strength);
            light->castsShadows = payload.value("casts_shadows", light->castsShadows);

            if (lightComponent->getLightType() == elix::engine::LightComponent::LightType::DIRECTIONAL)
            {
                if (auto *directionalLight = dynamic_cast<elix::engine::DirectionalLight *>(light.get()))
                    directionalLight->skyLightEnabled = payload.value("sky_light_enabled", directionalLight->skyLightEnabled);
            }
            else if (lightComponent->getLightType() == elix::engine::LightComponent::LightType::POINT)
            {
                if (auto *pointLight = dynamic_cast<elix::engine::PointLight *>(light.get()))
                {
                    pointLight->radius = payload.value("radius", pointLight->radius);
                    pointLight->falloff = payload.value("falloff", pointLight->falloff);
                }
            }
            else if (lightComponent->getLightType() == elix::engine::LightComponent::LightType::SPOT)
            {
                if (auto *spotLight = dynamic_cast<elix::engine::SpotLight *>(light.get()))
                {
                    spotLight->innerAngle = payload.value("inner_angle", spotLight->innerAngle);
                    spotLight->outerAngle = payload.value("outer_angle", spotLight->outerAngle);
                    spotLight->range = payload.value("range", spotLight->range);
                }
            }

            return true;
        }

        if (auto *scriptComponent = dynamic_cast<elix::engine::ScriptComponent *>(component))
        {
            if (payload.value("script_name", std::string{}) != scriptComponent->getScriptName())
            {
                outError = "Clipboard script does not match this script component";
                return false;
            }

            elix::engine::Script::ExposedVariablesMap serializedVariables;
            if (payload.contains("variables") && payload["variables"].is_object())
            {
                for (auto variableIt = payload["variables"].begin(); variableIt != payload["variables"].end(); ++variableIt)
                {
                    elix::engine::Script::ExposedVariable variable;
                    if (!scriptVariableFromJson(variableIt.value(), variable))
                        continue;
                    serializedVariables[variableIt.key()] = std::move(variable);
                }
            }

            scriptComponent->setSerializedVariables(serializedVariables);
            return true;
        }

        outError = "Clipboard paste is not supported for this component yet";
        return false;
    }

    void drawComponentClipboardToolbar(elix::editor::NotificationManager &notificationManager,
                                       elix::engine::Scene *scene,
                                       elix::engine::Entity *entity,
                                       elix::engine::ECS *component,
                                       const char *label)
    {
        if (!component)
            return;

        const std::string typeKey = componentClipboardTypeKey(component);
        if (typeKey.empty())
            return;

        ImGui::PushID(component);

        if (ImGui::SmallButton("Copy Settings"))
        {
            if (auto payload = serializeComponentForClipboard(entity, component); payload.has_value())
            {
                g_componentClipboard.hasData = true;
                g_componentClipboard.typeKey = typeKey;
                g_componentClipboard.label = label ? label : typeKey;
                g_componentClipboard.payload = std::move(payload.value());

                nlohmann::json clipboardJson{
                    {"type_key", g_componentClipboard.typeKey},
                    {"label", g_componentClipboard.label},
                    {"payload", g_componentClipboard.payload}};
                ImGui::SetClipboardText(clipboardJson.dump(2).c_str());
                notificationManager.showSuccess("Copied component settings: " + g_componentClipboard.label);
            }
            else
            {
                notificationManager.showError("Failed to copy component settings");
            }
        }

        ImGui::SameLine();

        const bool canPaste = g_componentClipboard.hasData && g_componentClipboard.typeKey == typeKey;
        if (!canPaste)
            ImGui::BeginDisabled();

        if (ImGui::SmallButton("Paste Settings"))
        {
            std::string error;
            if (applyComponentClipboardPayload(scene, entity, component, g_componentClipboard.payload, error))
                notificationManager.showSuccess("Pasted component settings: " + g_componentClipboard.label);
            else
                notificationManager.showError(error.empty() ? "Failed to paste component settings" : error);
        }

        if (!canPaste)
            ImGui::EndDisabled();

        if (g_componentClipboard.hasData)
        {
            ImGui::SameLine();
            if (canPaste)
                ImGui::TextDisabled("Clipboard ready");
            else
                ImGui::TextDisabled("Clipboard: %s", g_componentClipboard.label.c_str());
        }

        ImGui::Separator();
        ImGui::PopID();
    }

    bool createCollisionComponent(elix::engine::Scene *scene,
                                  elix::engine::Entity *entity,
                                  elix::engine::CollisionComponent::ShapeType shapeType)
    {
        if (!scene || !entity)
            return false;

        auto *transformComponent = entity->getComponent<elix::engine::Transform3DComponent>();
        if (!transformComponent)
            return false;

        if (entity->getComponent<elix::engine::CharacterMovementComponent>())
        {
            VX_EDITOR_WARNING_STREAM("Collision component add blocked: entity '" << entity->getName()
                                                                                 << "' already has CharacterMovementComponent.\n");
            return false;
        }

        if (auto *existingCollision = entity->getComponent<elix::engine::CollisionComponent>())
            destroyCollisionComponent(scene, entity, existingCollision);

        const glm::vec3 safeScale = glm::max(glm::abs(transformComponent->getScale()), glm::vec3(0.1f));
        glm::vec3 boxHalfExtents = glm::max(safeScale * 0.5f, glm::vec3(0.05f));
        float capsuleRadius = std::max(0.05f, std::max(safeScale.x, safeScale.z) * 0.25f);
        float capsuleHalfHeight = std::max(0.0f, safeScale.y * 0.5f - capsuleRadius);

        physx::PxShape *shape = nullptr;
        if (shapeType == elix::engine::CollisionComponent::ShapeType::BOX)
        {
            shape = scene->getPhysicsScene().createShape(physx::PxBoxGeometry(boxHalfExtents.x, boxHalfExtents.y, boxHalfExtents.z));
        }
        else
        {
            shape = scene->getPhysicsScene().createShape(physx::PxCapsuleGeometry(capsuleRadius, capsuleHalfHeight));
            if (shape)
                shape->setLocalPose(physx::PxTransform(physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0.0f, 0.0f, 1.0f))));
        }

        if (!shape)
            return false;

        if (auto *rigidBodyComponent = entity->getComponent<elix::engine::RigidBodyComponent>())
        {
            rigidBodyComponent->getRigidActor()->attachShape(*shape);
            if (auto *dynamicBody = rigidBodyComponent->getRigidActor()->is<physx::PxRigidDynamic>())
                physx::PxRigidBodyExt::updateMassAndInertia(*dynamicBody, 10.0f);

            entity->addComponent<elix::engine::CollisionComponent>(
                shape,
                shapeType,
                boxHalfExtents,
                capsuleRadius,
                capsuleHalfHeight,
                nullptr);
            return true;
        }

        auto *staticActor = scene->getPhysicsScene().createStatic(makePxTransformFromEntity(entity));
        if (!staticActor)
        {
            shape->release();
            return false;
        }

        staticActor->attachShape(*shape);
        entity->addComponent<elix::engine::CollisionComponent>(
            shape,
            shapeType,
            boxHalfExtents,
            capsuleRadius,
            capsuleHalfHeight,
            staticActor);
        return true;
    }

    bool hasOtherDirectionalLight(const elix::engine::Scene *scene, const elix::engine::Entity *excludeEntity)
    {
        if (!scene)
            return false;

        for (const auto &entity : scene->getEntities())
        {
            if (!entity || entity.get() == excludeEntity)
                continue;

            auto *lightComponent = entity->getComponent<elix::engine::LightComponent>();
            if (!lightComponent)
                continue;

            if (lightComponent->getLightType() == elix::engine::LightComponent::LightType::DIRECTIONAL)
                return true;
        }

        return false;
    }

    // Helper: add a component by type to an entity, with no-op lambda for single-line use.
    // Using engine:: prefix everywhere in lambdas avoids GCC template-argument-list
    // parse ambiguity that occurs with `using namespace` + `<Type>` inside lambda bodies.
    void registerBuiltinComponents()
    {
        auto &reg = elix::engine::ComponentRegistry::instance();

        // ── Common ───────────────────────────────────────────────────────────
        reg.registerComponent("Camera", "Common",
            [](elix::engine::Entity *e, elix::engine::Scene *, elix::engine::ComponentAddContext &)
            { e->addComponent<elix::engine::CameraComponent>(); });

        reg.registerComponent("Light", "Common",
            [](elix::engine::Entity *e, elix::engine::Scene *, elix::engine::ComponentAddContext &)
            { e->addComponent<elix::engine::LightComponent>(elix::engine::LightComponent::LightType::POINT); });

        reg.registerComponent("Audio Source", "Common",
            [](elix::engine::Entity *e, elix::engine::Scene *, elix::engine::ComponentAddContext &)
            { e->addComponent<elix::engine::AudioComponent>(); });

        reg.registerComponent("Animator", "Common",
            [](elix::engine::Entity *e, elix::engine::Scene *, elix::engine::ComponentAddContext &ctx)
            {
                if (e->getComponent<elix::engine::AnimatorComponent>())
                {
                    if (ctx.showWarning) ctx.showWarning("Animator already exists on this entity");
                    ctx.closePopup = false;
                    return;
                }
                e->addComponent<elix::engine::AnimatorComponent>();
            });

        reg.registerComponent("Reflection Probe", "Common",
            [](elix::engine::Entity *e, elix::engine::Scene *, elix::engine::ComponentAddContext &ctx)
            {
                if (e->getComponent<elix::engine::ReflectionProbeComponent>())
                {
                    if (ctx.showWarning) ctx.showWarning("Reflection Probe already exists on this entity");
                    ctx.closePopup = false;
                    return;
                }
                e->addComponent<elix::engine::ReflectionProbeComponent>();
            });

        reg.registerComponent("Particle System", "Common",
            [](elix::engine::Entity *e, elix::engine::Scene *, elix::engine::ComponentAddContext &)
            {
                namespace ps = elix::engine;
                auto *psComp = e->addComponent<ps::ParticleSystemComponent>();
                auto newSys = std::make_shared<ps::ParticleSystem>();
                newSys->name = "Particle System";
                auto *emitter = newSys->addEmitter("Emitter 0");
                emitter->addModule<ps::SpawnModule>();
                emitter->addModule<ps::LifetimeModule>();
                emitter->addModule<ps::InitialVelocityModule>();
                emitter->addModule<ps::SizeOverLifetimeModule>();
                emitter->addModule<ps::ForceModule>();
                emitter->addModule<ps::RendererModule>();
                emitter->addModule<ps::ColorOverLifetimeModule>();
                psComp->setParticleSystem(newSys);
            });

        reg.registerComponent("Decal", "Common",
            [](elix::engine::Entity *e, elix::engine::Scene *, elix::engine::ComponentAddContext &ctx)
            {
                if (e->getComponent<elix::engine::DecalComponent>())
                {
                    if (ctx.showWarning) ctx.showWarning("Decal already exists on this entity");
                    ctx.closePopup = false;
                    return;
                }
                e->addComponent<elix::engine::DecalComponent>();
            });

        reg.registerComponent("Sprite", "Common",
            [](elix::engine::Entity *e, elix::engine::Scene *, elix::engine::ComponentAddContext &ctx)
            {
                if (e->getComponent<elix::engine::SpriteComponent>())
                {
                    if (ctx.showWarning) ctx.showWarning("Sprite already exists on this entity");
                    ctx.closePopup = false;
                    return;
                }
                e->addComponent<elix::engine::SpriteComponent>();
            });

        // ── Physics ──────────────────────────────────────────────────────────
        reg.registerComponent("RigidBody", "Physics",
            [](elix::engine::Entity *e, elix::engine::Scene *s, elix::engine::ComponentAddContext &ctx)
            {
                if (e->getComponent<elix::engine::RagdollComponent>())
                {
                    if (ctx.showWarning) ctx.showWarning("Remove Ragdoll before adding RigidBody");
                    ctx.closePopup = false;
                    return;
                }

                physx::PxTransform transform = makePxTransformFromEntity(e);
                auto rigid = s->getPhysicsScene().createDynamic(transform);
                auto *rigidComponent = e->addComponent<elix::engine::RigidBodyComponent>(rigid);

                if (auto *col = e->getComponent<elix::engine::CollisionComponent>())
                {
                    if (col->getActor())
                    {
                        s->getPhysicsScene().removeActor(*col->getActor(), true, true);
                        col->removeActor();
                    }
                    rigidComponent->getRigidActor()->attachShape(*col->getShape());
                    if (auto *dyn = rigidComponent->getRigidActor()->is<physx::PxRigidDynamic>())
                        physx::PxRigidBodyExt::updateMassAndInertia(*dyn, 10.0f);
                }
            });

        reg.registerComponent("Character Movement", "Physics",
            [](elix::engine::Entity *e, elix::engine::Scene *s, elix::engine::ComponentAddContext &ctx)
            {
                if (e->getComponent<elix::engine::CharacterMovementComponent>())
                {
                    if (ctx.showWarning) ctx.showWarning("Character movement already exists");
                    ctx.closePopup = false;
                    return;
                }
                if (e->getComponent<elix::engine::CollisionComponent>())
                {
                    if (ctx.showWarning) ctx.showWarning("Remove Collision component before adding Character Movement");
                    ctx.closePopup = false;
                    return;
                }
                auto *transform = e->getComponent<elix::engine::Transform3DComponent>();
                const glm::vec3 safeScale = transform
                    ? glm::max(glm::abs(transform->getScale()), glm::vec3(0.1f))
                    : glm::vec3(1.0f);
                const float radius = std::max(0.1f, std::max(safeScale.x, safeScale.z) * 0.25f);
                const float height = std::max(0.5f, safeScale.y);
                e->addComponent<elix::engine::CharacterMovementComponent>(s, radius, height);
                if (ctx.showSuccess) ctx.showSuccess("Character movement added");
            });

        reg.registerComponent("Ragdoll", "Physics",
            [](elix::engine::Entity *e, elix::engine::Scene *s, elix::engine::ComponentAddContext &ctx)
            {
                if (e->getComponent<elix::engine::RagdollComponent>())
                {
                    if (ctx.showWarning) ctx.showWarning("Ragdoll already exists on this entity");
                    ctx.closePopup = false;
                    return;
                }
                if (!e->getComponent<elix::engine::SkeletalMeshComponent>())
                {
                    if (ctx.showWarning) ctx.showWarning("Ragdoll requires a SkeletalMeshComponent");
                    ctx.closePopup = false;
                    return;
                }
                if (e->getComponent<elix::engine::RigidBodyComponent>())
                {
                    if (ctx.showWarning) ctx.showWarning("Remove RigidBody before adding Ragdoll");
                    ctx.closePopup = false;
                    return;
                }

                auto *ragdoll = e->addComponent<elix::engine::RagdollComponent>(s);
                if (!ragdoll)
                    return;

                ragdoll->autoGenerateHumanoidProfile();
                if (ctx.showSuccess)
                    ctx.showSuccess("Ragdoll added");
            });

        reg.registerComponent("Box Collision", "Physics",
            [](elix::engine::Entity *e, elix::engine::Scene *s, elix::engine::ComponentAddContext &ctx)
            {
                if (createCollisionComponent(s, e, elix::engine::CollisionComponent::ShapeType::BOX))
                {
                    if (ctx.showSuccess) ctx.showSuccess("Box collision added");
                }
                else
                {
                    if (ctx.showError) ctx.showError("Failed to add box collision");
                }
            });

        reg.registerComponent("Capsule Collision", "Physics",
            [](elix::engine::Entity *e, elix::engine::Scene *s, elix::engine::ComponentAddContext &ctx)
            {
                if (createCollisionComponent(s, e, elix::engine::CollisionComponent::ShapeType::CAPSULE))
                {
                    if (ctx.showSuccess) ctx.showSuccess("Capsule collision added");
                }
                else
                {
                    if (ctx.showError) ctx.showError("Failed to add capsule collision");
                }
            });
    }
}

ELIX_NESTED_NAMESPACE_BEGIN(editor)

bool EntityDetailsView::canDraw(const Editor &editor) const
{
    return editor.m_detailsContext != Editor::DetailsContext::Asset &&
           editor.m_selectedEntity != nullptr;
}

void EntityDetailsView::draw(Editor &editor)
{
    // Register built-in components once on first draw.
    static bool s_componentsRegistered = []
    {
        registerBuiltinComponents();
        return true;
    }();
    (void)s_componentsRegistered;

    using EditorMode = Editor::EditorMode;

    auto *&m_selectedEntity = editor.m_selectedEntity;
    auto &m_scene = editor.m_scene;
    auto &m_projectScriptsRegister = editor.m_projectScriptsRegister;
    auto &m_loadedGameModulePath = editor.m_loadedGameModulePath;
    auto &m_notificationManager = editor.m_notificationManager;
    auto &m_currentMode = editor.m_currentMode;
    auto &m_selectedMeshSlot = editor.m_selectedMeshSlot;
    auto &m_lastScrolledMeshSlot = editor.m_lastScrolledMeshSlot;
    auto &m_assetsPreviewSystem = editor.m_assetsPreviewSystem;
    auto &m_showCollisionBounds = editor.m_showCollisionBounds;
    auto &m_enableCollisionBoundsEditing = editor.m_enableCollisionBoundsEditing;
    auto &m_currentProject = editor.m_currentProject;

    if (!m_selectedEntity)
    {
        ImGui::Text("Select an object or asset to view details");
        return;
    }

    char buffer[128];
    std::strncpy(buffer, m_selectedEntity->getName().c_str(), sizeof(buffer));
    if (ImGui::InputText("##Name", buffer, sizeof(buffer)))
        m_selectedEntity->setName(std::string(buffer));

    ImGui::SameLine();

    bool isEntityEnabled = m_selectedEntity->isEnabled();
    if (ImGui::Checkbox("Enabled", &isEntityEnabled))
        m_selectedEntity->setEnabled(isEntityEnabled);

    ImGui::SameLine();

    if (ImGui::Button("Add component"))
    {
        if (ImGui::IsPopupOpen("AddComponentPopup"))
            ImGui::CloseCurrentPopup();
        else
            ImGui::OpenPopup("AddComponentPopup");
    }

    bool componentRemovedFromPopup = false;
    ImGui::SameLine();
    if (ImGui::Button("Remove component"))
        ImGui::OpenPopup("RemoveComponentPopup");

    if (ImGui::BeginPopup("RemoveComponentPopup"))
    {
        ImGui::TextDisabled("Transform3DComponent cannot be removed.");
        ImGui::Separator();

        if (m_selectedEntity->getComponent<engine::LightComponent>() && ImGui::MenuItem("Light"))
        {
            m_selectedEntity->removeComponent<engine::LightComponent>();
            componentRemovedFromPopup = true;
        }

        if (m_selectedEntity->getComponent<engine::StaticMeshComponent>() && ImGui::MenuItem("Static Mesh"))
        {
            m_selectedEntity->removeComponent<engine::StaticMeshComponent>();
            componentRemovedFromPopup = true;
        }

        if (m_selectedEntity->getComponent<engine::SkeletalMeshComponent>() && ImGui::MenuItem("Skeletal Mesh"))
        {
            m_selectedEntity->removeComponent<engine::RagdollComponent>();
            m_selectedEntity->removeComponent<engine::AnimatorComponent>();
            m_selectedEntity->removeComponent<engine::SkeletalMeshComponent>();
            componentRemovedFromPopup = true;
        }

        if (m_selectedEntity->getComponent<engine::AnimatorComponent>() && ImGui::MenuItem("Animator"))
        {
            m_selectedEntity->removeComponent<engine::AnimatorComponent>();
            componentRemovedFromPopup = true;
        }

        if (m_selectedEntity->getComponent<engine::CameraComponent>() && ImGui::MenuItem("Camera"))
        {
            m_selectedEntity->removeComponent<engine::CameraComponent>();
            componentRemovedFromPopup = true;
        }

        if (auto *rigidBodyComponent = m_selectedEntity->getComponent<engine::RigidBodyComponent>();
            rigidBodyComponent && ImGui::MenuItem("RigidBody"))
        {
            destroyRigidBodyComponent(m_scene.get(), m_selectedEntity, rigidBodyComponent);
            componentRemovedFromPopup = true;
        }

        if (m_selectedEntity->getComponent<engine::CharacterMovementComponent>() && ImGui::MenuItem("Character Movement"))
        {
            m_selectedEntity->removeComponent<engine::CharacterMovementComponent>();
            componentRemovedFromPopup = true;
        }

        if (m_selectedEntity->getComponent<engine::RagdollComponent>() && ImGui::MenuItem("Ragdoll"))
        {
            m_selectedEntity->removeComponent<engine::RagdollComponent>();
            componentRemovedFromPopup = true;
        }

        if (auto *collisionComponent = m_selectedEntity->getComponent<engine::CollisionComponent>();
            collisionComponent && ImGui::MenuItem("Collision"))
        {
            destroyCollisionComponent(m_scene.get(), m_selectedEntity, collisionComponent);
            componentRemovedFromPopup = true;
        }

        if (!m_selectedEntity->getComponents<engine::AudioComponent>().empty() && ImGui::MenuItem("Audio Source (All)"))
        {
            m_selectedEntity->removeComponent<engine::AudioComponent>();
            componentRemovedFromPopup = true;
        }

        if (!m_selectedEntity->getComponents<engine::ScriptComponent>().empty() && ImGui::MenuItem("Script (All)"))
        {
            m_selectedEntity->removeComponent<engine::ScriptComponent>();
            componentRemovedFromPopup = true;
        }

        if (!m_selectedEntity->getComponents<engine::ParticleSystemComponent>().empty() && ImGui::MenuItem("Particle System (All)"))
        {
            m_selectedEntity->removeComponent<engine::ParticleSystemComponent>();
            componentRemovedFromPopup = true;
        }

        if (componentRemovedFromPopup)
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    if (componentRemovedFromPopup)
    {
        return;
    }

    const auto *parentEntity = m_selectedEntity->getParent();
    ImGui::Text("Parent: %s", parentEntity ? parentEntity->getName().c_str() : "<Root>");
    if (parentEntity && ImGui::Button("Detach From Parent"))
        m_selectedEntity->clearParent();

    ImGui::Separator();
    ImGui::Text("Tags");
    ImGui::SameLine();

    const auto &tagSet = m_selectedEntity->getTags();
    std::vector<std::string> tags(tagSet.begin(), tagSet.end());
    std::sort(tags.begin(), tags.end());

    std::string tagToRemove;
    for (const auto &tag : tags)
    {
        if (tag == "__dontdestroy__")
            continue;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.75f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.55f, 0.85f, 1.0f));
        const std::string removeLabel = "x##tag_" + tag;
        ImGui::SmallButton(tag.c_str());
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
        if (ImGui::SmallButton(removeLabel.c_str()))
            tagToRemove = tag;
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
    }
    ImGui::NewLine();

    if (!tagToRemove.empty())
        m_selectedEntity->removeTag(tagToRemove);

    static char s_tagBuffer[64] = "";
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##NewTag", "New tag...", s_tagBuffer, sizeof(s_tagBuffer));
    ImGui::SameLine();
    if (ImGui::SmallButton("Add Tag") && s_tagBuffer[0] != '\0')
    {
        m_selectedEntity->addTag(std::string(s_tagBuffer));
        s_tagBuffer[0] = '\0';
    }
    ImGui::Separator();

    if (ImGui::BeginPopup("AddComponentPopup"))
    {
        ImGui::Text("Scripting");

        ImGui::Button("New C++ class");

        if (!m_projectScriptsRegister)
            ImGui::TextDisabled("Build project to load scripts");
        else
        {
            const auto &registeredScripts = m_projectScriptsRegister->getScripts();

            if (registeredScripts.empty())
            {
                ImGui::TextDisabled("No scripts registered in module");
                ImGui::Separator();
                ImGui::TextWrapped("Loaded module:");
                if (!m_loadedGameModulePath.empty())
                    ImGui::TextWrapped("%s", m_loadedGameModulePath.c_str());
                ImGui::Separator();
            }

            std::vector<std::string> scriptNames;
            scriptNames.reserve(registeredScripts.size());

            for (const auto &[scriptName, _] : registeredScripts)
                scriptNames.push_back(scriptName);

            std::sort(scriptNames.begin(), scriptNames.end());

            ImGui::PushID("RegisteredScripts");
            for (const auto &scriptName : scriptNames)
            {
                ImGui::PushID(scriptName.c_str());
                if (!ImGui::Button(scriptName.c_str()))
                {
                    ImGui::PopID();
                    continue;
                }

                auto *scriptInstance = m_projectScriptsRegister->createScript(scriptName);
                if (!scriptInstance)
                {
                    VX_EDITOR_ERROR_STREAM("Failed to create script '" << scriptName << "'\n");
                    m_notificationManager.showError("Failed to create script");
                    ImGui::PopID();
                    continue;
                }

                auto *scriptComponent = m_selectedEntity->addComponent<engine::ScriptComponent>(scriptName, scriptInstance);

                if (m_currentMode == EditorMode::PLAY && scriptComponent)
                    scriptComponent->onAttach();

                VX_EDITOR_INFO_STREAM("Attached script '" << scriptName << "' to entity '" << m_selectedEntity->getName() << "'\n");
                m_notificationManager.showSuccess("Attached script: " + scriptName);
                ImGui::CloseCurrentPopup();
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }

        // ── Engine components from registry (grouped by category) ────────────
        {
            engine::ComponentAddContext ctx;
            ctx.showSuccess = [&m_notificationManager](const std::string &msg) { m_notificationManager.showSuccess(msg); };
            ctx.showWarning = [&m_notificationManager](const std::string &msg) { m_notificationManager.showWarning(msg); };
            ctx.showError   = [&m_notificationManager](const std::string &msg) { m_notificationManager.showError(msg); };

            const auto &entries = engine::ComponentRegistry::instance().getEntries();
            std::string lastCategory;

            for (const auto &entry : entries)
            {
                if (entry.category != lastCategory)
                {
                    ImGui::Separator();
                    ImGui::Text("%s", entry.category.c_str());
                    lastCategory = entry.category;
                }

                ImGui::PushID(entry.displayName.c_str());
                if (ImGui::Button(entry.displayName.c_str()))
                {
                    ctx.closePopup = true;
                    entry.addFn(m_selectedEntity, m_scene.get(), ctx);
                    if (ctx.closePopup)
                        ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
        }

        ImGui::Separator();

        ImGui::EndPopup();
    }

    for (const auto &[_, component] : m_selectedEntity->getSingleComponents())
    {
        if (auto transformComponent = dynamic_cast<engine::Transform3DComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                drawComponentClipboardToolbar(m_notificationManager, m_scene.get(), m_selectedEntity, transformComponent, "Transform");
                constexpr float kLabelW = 68.0f;
                ImGui::Spacing();

                auto position = transformComponent->getPosition();
                auto euler = transformComponent->getEulerDegrees();
                auto scale = transformComponent->getScale();

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Position");
                ImGui::SameLine(kLabelW);
                ImGui::SetNextItemWidth(-1);
                if (drawVec3Control("##tfPos", position, 0.0f, 0.01f))
                    transformComponent->setPosition(position);

                ImGui::Spacing();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Rotation");
                ImGui::SameLine(kLabelW);
                ImGui::SetNextItemWidth(-1);
                if (drawVec3Control("##tfRot", euler, 0.0f, 0.1f))
                    transformComponent->setEulerDegrees(euler);

                ImGui::Spacing();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Scale");
                ImGui::SameLine(kLabelW);
                ImGui::SetNextItemWidth(-1);
                if (drawVec3Control("##tfScl", scale, 1.0f, 0.01f))
                    transformComponent->setScale(scale);

                ImGui::Spacing();
            }
        }
        else if (auto lightComponent = dynamic_cast<engine::LightComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                drawComponentClipboardToolbar(m_notificationManager, m_scene.get(), m_selectedEntity, lightComponent, "Light");
                auto lightType = lightComponent->getLightType();
                auto light = lightComponent->getLight();

                static const std::vector<const char *> lightTypes{
                    "Directional",
                    "Spot",
                    "Point"};

                int currentLighType = 0;

                switch (lightType)
                {
                case engine::LightComponent::LightType::DIRECTIONAL:
                    currentLighType = 0;
                    break;
                case engine::LightComponent::LightType::SPOT:
                    currentLighType = 1;
                    break;
                case engine::LightComponent::LightType::POINT:
                    currentLighType = 2;
                    break;
                };

                if (ImGui::Combo("Light type", &currentLighType, lightTypes.data(), lightTypes.size()))
                {
                    engine::LightComponent::LightType requestedType = lightType;
                    if (currentLighType == 0)
                        requestedType = engine::LightComponent::LightType::DIRECTIONAL;
                    else if (currentLighType == 1)
                        requestedType = engine::LightComponent::LightType::SPOT;
                    else if (currentLighType == 2)
                        requestedType = engine::LightComponent::LightType::POINT;

                    if (requestedType == engine::LightComponent::LightType::DIRECTIONAL &&
                        hasOtherDirectionalLight(m_scene.get(), m_selectedEntity))
                    {
                        VX_EDITOR_ERROR_STREAM("Only one directional light is allowed in a scene.\n");
                        m_notificationManager.showError("Only one directional light is allowed");
                    }
                    else
                    {
                        lightComponent->changeLightType(requestedType);
                        lightType = lightComponent->getLightType();
                        light = lightComponent->getLight();
                    }
                }

                if (auto *transformComponent = m_selectedEntity->getComponent<engine::Transform3DComponent>())
                {
                    const glm::vec3 worldPosition = transformComponent->getWorldPosition();
                    ImGui::Text("Light position: %.2f %.2f %.2f", worldPosition.x, worldPosition.y, worldPosition.z);
                }
                ImGui::ColorEdit3("Light color", &light->color.x);
                ImGui::DragFloat("Light strength", &light->strength, 0.1f, 0.0f, 150.0f);
                ImGui::Checkbox("Cast Shadows", &light->castsShadows);

                if (lightType == engine::LightComponent::LightType::POINT)
                {
                    auto pointLight = dynamic_cast<engine::PointLight *>(light.get());
                    ImGui::DragFloat("Light radius", &pointLight->radius, 0.1, 0.0f, 360.0f);
                }
                else if (lightType == engine::LightComponent::LightType::DIRECTIONAL)
                {
                    auto directionalLight = dynamic_cast<engine::DirectionalLight *>(light.get());
                    ImGui::Text("Light direction: %.2f %.2f %.2f",
                                directionalLight->direction.x,
                                directionalLight->direction.y,
                                directionalLight->direction.z);
                    ImGui::Checkbox("Enable Sky Light", &directionalLight->skyLightEnabled);
                }
                else if (lightType == engine::LightComponent::LightType::SPOT)
                {
                    auto spotLight = dynamic_cast<engine::SpotLight *>(light.get());
                    ImGui::Text("Light direction: %.2f %.2f %.2f",
                                spotLight->direction.x,
                                spotLight->direction.y,
                                spotLight->direction.z);
                    ImGui::DragFloat("Inner", &spotLight->innerAngle);
                    ImGui::DragFloat("Outer", &spotLight->outerAngle);
                }
            }
        }
        else if (auto staticComponent = dynamic_cast<engine::StaticMeshComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Static mesh", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto &meshes = staticComponent->getMeshes();

                ImGui::PushID("StaticMeshAllSlotsMaterial");
                ImGui::Button("Drop .elixmat to apply to all slots##StaticMeshAllSlotsDrop");
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                    {
                        std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                        std::string extension = std::filesystem::path(droppedPath).extension().string();
                        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
                                       { return static_cast<char>(std::tolower(character)); });

                        if (extension == ".elixmat")
                        {
                            if (editor.applyMaterialToSelectedEntity(droppedPath, std::nullopt, true))
                                m_notificationManager.showSuccess("Material applied to all slots");
                            else
                                m_notificationManager.showError("Failed to apply material");
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopID();
                ImGui::Separator();

                for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
                {
                    const auto &mesh = meshes[meshIndex];
                    const bool isPickedMeshSlot = m_selectedMeshSlot.has_value() && m_selectedMeshSlot.value() == meshIndex;
                    std::string overridePath = staticComponent->getMaterialOverridePath(meshIndex);
                    const bool hasOverride = !overridePath.empty();

                    std::string materialLabel = hasOverride
                                                    ? std::filesystem::path(overridePath).filename().string()
                                                    : std::string("<Default>");
                    const std::string meshName = mesh.name.empty() ? ("Mesh_" + std::to_string(meshIndex)) : mesh.name;

                    ImGui::PushID(static_cast<int>(meshIndex));
                    ImGui::Text("Slot %zu (%s)", meshIndex, meshName.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", materialLabel.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", isPickedMeshSlot ? "[picked]" : "");

                    VkDescriptorSet previewDescriptorSet = m_assetsPreviewSystem.getPlaceholder();
                    if (hasOverride)
                        previewDescriptorSet = m_assetsPreviewSystem.getOrRequestMaterialPreview(overridePath);

                    if (isPickedMeshSlot && m_lastScrolledMeshSlot != m_selectedMeshSlot)
                    {
                        ImGui::SetScrollHereY(0.5f);
                        m_lastScrolledMeshSlot = m_selectedMeshSlot;
                    }

                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, isPickedMeshSlot ? 2.0f : 1.0f);
                    if (isPickedMeshSlot)
                        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 170, 40, 255));
                    ImGui::ImageButton("##MaterialPreview", previewDescriptorSet, ImVec2(52.0f, 52.0f));
                    if (isPickedMeshSlot)
                        ImGui::PopStyleColor();
                    ImGui::PopStyleVar();

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                        m_selectedMeshSlot = static_cast<uint32_t>(meshIndex);

                    if (!hasOverride && ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        if (mesh.material.albedoTexture.empty())
                            ImGui::TextUnformatted("No override material");
                        else
                            ImGui::Text("Mesh Albedo: %s", mesh.material.albedoTexture.c_str());
                        ImGui::EndTooltip();
                    }

                    if (hasOverride && ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("Material: %s", overridePath.c_str());
                        ImGui::TextUnformatted("Double-click to open material editor");
                        ImGui::EndTooltip();
                    }

                    if (hasOverride && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        editor.openMaterialEditor(overridePath);

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                        {
                            std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                            std::string extension = std::filesystem::path(droppedPath).extension().string();
                            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
                                           { return static_cast<char>(std::tolower(character)); });

                            if (extension == ".elixmat")
                            {
                                if (editor.applyMaterialToSelectedEntity(droppedPath, meshIndex))
                                    m_notificationManager.showSuccess("Material applied to slot");
                                else
                                    m_notificationManager.showError("Failed to apply material");
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::SameLine();

                    ImGui::BeginGroup();
                    ImGui::TextUnformatted("Override Material");

                    if (hasOverride)
                        ImGui::TextWrapped("%s", overridePath.c_str());
                    else
                        ImGui::TextDisabled("<None>");

                    if (hasOverride && ImGui::Button("Open Material Editor"))
                        editor.openMaterialEditor(overridePath);

                    if (hasOverride && ImGui::Button("Clear Override"))
                    {
                        staticComponent->clearMaterialOverride(meshIndex);
                    }

                    ImGui::EndGroup();
                    ImGui::Separator();
                    ImGui::PopID();
                }
            }
        }
        else if (auto skeletalMeshComponent = dynamic_cast<engine::SkeletalMeshComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Skeletal mesh", ImGuiTreeNodeFlags_DefaultOpen))
            {
                bool showBones = editor.m_showSelectedSkeletalBones;
                if (ImGui::Checkbox("Show Bones", &showBones))
                {
                    editor.m_showSelectedSkeletalBones = showBones;
                    if (!showBones)
                        editor.clearSelectedSkeletalBoneSelection();
                }

                ImGui::SameLine();
                ImGui::TextDisabled("%zu bones", skeletalMeshComponent->getSkeleton().getBonesCount());

                if (editor.m_showSelectedSkeletalBones)
                {
                    const std::string selectedBoneName = editor.getSelectedSkeletalBoneName();
                    if (!selectedBoneName.empty())
                        ImGui::Text("Selected Bone: %s", selectedBoneName.c_str());
                    else
                        ImGui::TextDisabled("Click a bone in the viewport to show its name.");
                }

                ImGui::Separator();

                const auto &meshes = skeletalMeshComponent->getMeshes();

                ImGui::PushID("SkeletalMeshAllSlotsMaterial");
                ImGui::Button("Drop .elixmat to apply to all slots##SkeletalMeshAllSlotsDrop");
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                    {
                        std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                        std::string extension = std::filesystem::path(droppedPath).extension().string();
                        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
                                       { return static_cast<char>(std::tolower(character)); });

                        if (extension == ".elixmat")
                        {
                            if (editor.applyMaterialToSelectedEntity(droppedPath, std::nullopt, true))
                                m_notificationManager.showSuccess("Material applied to all slots");
                            else
                                m_notificationManager.showError("Failed to apply material");
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopID();
                ImGui::Separator();

                for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
                {
                    const auto &mesh = meshes[meshIndex];
                    const bool isPickedMeshSlot = m_selectedMeshSlot.has_value() && m_selectedMeshSlot.value() == meshIndex;
                    std::string overridePath = skeletalMeshComponent->getMaterialOverridePath(meshIndex);
                    const bool hasOverride = !overridePath.empty();

                    std::string materialLabel = hasOverride
                                                    ? std::filesystem::path(overridePath).filename().string()
                                                    : std::string("<Default>");
                    const std::string meshName = mesh.name.empty() ? ("Mesh_" + std::to_string(meshIndex)) : mesh.name;

                    ImGui::PushID(static_cast<int>(meshIndex));
                    ImGui::Text("Slot %zu (%s)", meshIndex, meshName.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", materialLabel.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", isPickedMeshSlot ? "[picked]" : "");

                    VkDescriptorSet previewDescriptorSet = m_assetsPreviewSystem.getPlaceholder();
                    if (hasOverride)
                        previewDescriptorSet = m_assetsPreviewSystem.getOrRequestMaterialPreview(overridePath);

                    if (isPickedMeshSlot && m_lastScrolledMeshSlot != m_selectedMeshSlot)
                    {
                        ImGui::SetScrollHereY(0.5f);
                        m_lastScrolledMeshSlot = m_selectedMeshSlot;
                    }

                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, isPickedMeshSlot ? 2.0f : 1.0f);
                    if (isPickedMeshSlot)
                        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 170, 40, 255));
                    ImGui::ImageButton("##MaterialPreview", previewDescriptorSet, ImVec2(52.0f, 52.0f));
                    if (isPickedMeshSlot)
                        ImGui::PopStyleColor();
                    ImGui::PopStyleVar();

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                        m_selectedMeshSlot = static_cast<uint32_t>(meshIndex);

                    if (!hasOverride && ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        if (mesh.material.albedoTexture.empty())
                            ImGui::TextUnformatted("No override material");
                        else
                            ImGui::Text("Mesh Albedo: %s", mesh.material.albedoTexture.c_str());
                        ImGui::EndTooltip();
                    }

                    if (hasOverride && ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("Material: %s", overridePath.c_str());
                        ImGui::TextUnformatted("Double-click to open material editor");
                        ImGui::EndTooltip();
                    }

                    if (hasOverride && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        editor.openMaterialEditor(overridePath);

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                        {
                            std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                            std::string extension = std::filesystem::path(droppedPath).extension().string();
                            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
                                           { return static_cast<char>(std::tolower(character)); });

                            if (extension == ".elixmat")
                            {
                                if (editor.applyMaterialToSelectedEntity(droppedPath, meshIndex))
                                    m_notificationManager.showSuccess("Material applied to slot");
                                else
                                    m_notificationManager.showError("Failed to apply material");
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::SameLine();

                    ImGui::BeginGroup();
                    ImGui::TextUnformatted("Override Material");

                    if (hasOverride)
                        ImGui::TextWrapped("%s", overridePath.c_str());
                    else
                        ImGui::TextDisabled("<None>");

                    if (hasOverride && ImGui::Button("Open Material Editor"))
                        editor.openMaterialEditor(overridePath);

                    if (hasOverride && ImGui::Button("Clear Override"))
                    {
                        skeletalMeshComponent->clearMaterialOverride(meshIndex);
                    }

                    ImGui::EndGroup();
                    ImGui::Separator();
                    ImGui::PopID();
                }
            }
        }
        else if (auto animatorComponent = dynamic_cast<engine::AnimatorComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Animator", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto &animations = animatorComponent->getAnimations();

                // Always-visible drop zone for adding animation clips
                {
                    const ImVec2 dropSize(-1.0f, animations.empty() ? 48.0f : 28.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.4f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.6f));
                    ImGui::Button(animations.empty() ? "Drop .anim.elixasset to load animations" : "+ Drop .anim.elixasset to add clips", dropSize);
                    ImGui::PopStyleColor(2);

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                        {
                            const std::string droppedPath(static_cast<const char *>(payload->Data), payload->DataSize - 1);
                            const std::string lower = [&droppedPath]()
                            {
                                std::string s = droppedPath;
                                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                                               { return static_cast<char>(std::tolower(c)); });
                                return s;
                            }();

                            if (lower.size() > std::strlen(".anim.elixasset") &&
                                lower.rfind(".anim.elixasset") == lower.size() - std::strlen(".anim.elixasset"))
                            {
                                const std::string normalizedAnimationAssetPath = std::filesystem::path(droppedPath).lexically_normal().string();
                                const auto &externalAnimationAssetPaths = animatorComponent->getExternalAnimationAssetPaths();
                                if (std::find(externalAnimationAssetPaths.begin(), externalAnimationAssetPaths.end(), normalizedAnimationAssetPath) != externalAnimationAssetPaths.end())
                                {
                                    editor.m_notificationManager.showInfo("Animation asset already added");
                                }
                                else if (auto animAsset = engine::AssetsLoader::loadAnimationAsset(normalizedAnimationAssetPath);
                                         animAsset.has_value() && !animAsset->animations.empty())
                                {
                                    auto *skelComp = m_selectedEntity->getComponent<engine::SkeletalMeshComponent>();
                                    engine::Skeleton *skel = skelComp ? &skelComp->getSkeleton() : nullptr;

                                    // Append new clips to existing ones
                                    std::vector<engine::Animation> merged = animatorComponent->getAnimations();
                                    merged.insert(merged.end(), animAsset->animations.begin(), animAsset->animations.end());
                                    animatorComponent->setAnimations(merged, skel);
                                    animatorComponent->addExternalAnimationAssetPath(normalizedAnimationAssetPath);
                                    if (animatorComponent->getSelectedAnimationIndex() < 0)
                                        animatorComponent->setSelectedAnimationIndex(0);
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                }

                if (!animations.empty())
                {
                    int selectedAnimationIndex = animatorComponent->getSelectedAnimationIndex();
                    if (selectedAnimationIndex < 0 || selectedAnimationIndex >= static_cast<int>(animations.size()))
                    {
                        selectedAnimationIndex = 0;
                        animatorComponent->setSelectedAnimationIndex(selectedAnimationIndex);
                    }

                    const std::string fallbackName = "<Unnamed>";
                    const std::string &selectedName = animations[selectedAnimationIndex].name.empty() ? fallbackName : animations[selectedAnimationIndex].name;

                    if (ImGui::BeginCombo("Clip", selectedName.c_str()))
                    {
                        for (int animationIndex = 0; animationIndex < static_cast<int>(animations.size()); ++animationIndex)
                        {
                            const std::string &animationName = animations[animationIndex].name.empty() ? fallbackName : animations[animationIndex].name;
                            const bool isSelected = animationIndex == selectedAnimationIndex;

                            ImGui::PushID(animationIndex);
                            if (ImGui::Selectable(animationName.c_str(), isSelected))
                            {
                                selectedAnimationIndex = animationIndex;
                                animatorComponent->setSelectedAnimationIndex(selectedAnimationIndex);
                            }
                            ImGui::PopID();

                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }

                        ImGui::EndCombo();
                    }

                    bool isLooped = animatorComponent->isAnimationLooped();
                    if (ImGui::Checkbox("Loop", &isLooped))
                        animatorComponent->setAnimationLooped(isLooped);

                    bool isPaused = animatorComponent->isAnimationPaused();
                    if (ImGui::Checkbox("Paused", &isPaused))
                        animatorComponent->setAnimationPaused(isPaused);

                    float animationSpeed = animatorComponent->getAnimationSpeed();
                    if (ImGui::DragFloat("Speed", &animationSpeed, 0.01f, 0.01f, 4.0f, "%.2f"))
                        animatorComponent->setAnimationSpeed(animationSpeed);

                    if (animatorComponent->isAnimationPlaying())
                    {
                        const float duration = animatorComponent->getCurrentAnimationDuration();
                        float currentTime = animatorComponent->getCurrentTime();

                        if (duration > 0.0f)
                        {
                            if (ImGui::SliderFloat("Time", &currentTime, 0.0f, duration))
                                animatorComponent->setCurrentTime(currentTime);

                            ImGui::Text("Time: %.2f / %.2f", currentTime, duration);
                        }
                    }

                    if (ImGui::Button("Play Selected"))
                        animatorComponent->playAnimationByIndex(static_cast<size_t>(selectedAnimationIndex), animatorComponent->isAnimationLooped());

                    ImGui::SameLine();

                    if (ImGui::Button("Stop"))
                        animatorComponent->stopAnimation();

                    if (m_currentMode != EditorMode::EDIT)
                        ImGui::TextDisabled("Animation preview updates in Edit mode");

                    ImGui::Separator();
                    if (ImGui::Button("Clear Animations"))
                    {
                        animatorComponent->stopAnimation();
                        animatorComponent->setAnimations({}, nullptr);
                        animatorComponent->setExternalAnimationAssetPaths({});
                    }
                }

                ImGui::Separator();
                ImGui::TextUnformatted("Animation Tree");

                if (animatorComponent->hasTree())
                {
                    const engine::AnimationTree *tree = animatorComponent->getTree();
                    ImGui::Text("Tree: %s", tree->name.c_str());

                    ImGui::SameLine();
                    if (ImGui::SmallButton("Open Editor"))
                        editor.openAnimationTreeEditor(tree->assetPath, animatorComponent,
                                                       m_selectedEntity->getComponent<engine::SkeletalMeshComponent>());

                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear"))
                    {
                        animatorComponent->clearTree();
                        tree = animatorComponent->getTree(); // now null — prevents dangling access below
                    }

                    // Runtime state info
                    const std::string curState = animatorComponent->getCurrentStateName();
                    const std::string statePath = animatorComponent->getCurrentStatePath();
                    const std::string machinePath = animatorComponent->getActiveMachinePath();
                    ImGui::Text("State: %s", curState.empty() ? "(none)" : curState.c_str());
                    ImGui::TextDisabled("State Path: %s", statePath.empty() ? "(none)" : statePath.c_str());
                    ImGui::TextDisabled("Machine Path: %s", machinePath.empty() ? "(none)" : machinePath.c_str());
                    if (animatorComponent->isInTransition())
                        ImGui::TextDisabled("  (transitioning %.0f%%)", animatorComponent->getCurrentStateNormalizedTime() * 100.0f);

                    bool ignoreRootBoneY = animatorComponent->getIgnoreRootBoneY();
                    if (ImGui::Checkbox("Ignore Root Bone Y", &ignoreRootBoneY))
                        animatorComponent->setIgnoreRootBoneY(ignoreRootBoneY);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Locks the root bone vertical translation to the bind pose. Useful for controller-driven characters.");

                    // Parameter live controls
                    if (tree && !tree->parameters.empty())
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Parameters:");
                        for (const auto &param : tree->parameters)
                        {
                            ImGui::PushID(param.name.c_str());
                            switch (param.type)
                            {
                            case engine::AnimationTreeParameter::Type::Float:
                            {
                                float v = animatorComponent->getFloat(param.name);
                                if (ImGui::DragFloat(param.name.c_str(), &v, 0.01f))
                                    animatorComponent->setFloat(param.name, v);
                                break;
                            }
                            case engine::AnimationTreeParameter::Type::Bool:
                            {
                                bool v = animatorComponent->getBool(param.name);
                                if (ImGui::Checkbox(param.name.c_str(), &v))
                                    animatorComponent->setBool(param.name, v);
                                break;
                            }
                            case engine::AnimationTreeParameter::Type::Int:
                            {
                                int v = animatorComponent->getInt(param.name);
                                if (ImGui::InputInt(param.name.c_str(), &v))
                                    animatorComponent->setInt(param.name, v);
                                break;
                            }
                            case engine::AnimationTreeParameter::Type::Trigger:
                            {
                                if (ImGui::Button(("Fire: " + param.name).c_str()))
                                    animatorComponent->setTrigger(param.name);
                                break;
                            }
                            }
                            ImGui::PopID();
                        }
                    }
                }
                else
                {
                    // Drop zone for .animtree.elixasset
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.4f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.6f));
                    ImGui::Button("Drop .animtree.elixasset here", ImVec2(-1.0f, 32.0f));
                    ImGui::PopStyleColor(2);

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                        {
                            const std::string droppedPath(static_cast<const char *>(payload->Data), payload->DataSize - 1);
                            const std::string lower = [&droppedPath]()
                            {
                                std::string s = droppedPath;
                                std::transform(s.begin(), s.end(), s.begin(),
                                               [](unsigned char c)
                                               { return static_cast<char>(std::tolower(c)); });
                                return s;
                            }();

                            constexpr const char *animTreeSuffix = ".animtree.elixasset";
                            if (lower.size() > std::strlen(animTreeSuffix) &&
                                lower.rfind(animTreeSuffix) == lower.size() - std::strlen(animTreeSuffix))
                            {
                                auto *skelComp = m_selectedEntity->getComponent<engine::SkeletalMeshComponent>();
                                if (skelComp)
                                    animatorComponent->bindSkeleton(&skelComp->getSkeleton());
                                animatorComponent->loadTree(droppedPath);
                                if (!animatorComponent->hasTree())
                                    editor.m_notificationManager.showError("Failed to load animation tree");
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                }
            }
        }
        else if (auto ragdollComponent = dynamic_cast<engine::RagdollComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Ragdoll", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto *skeletalMeshComponent = m_selectedEntity->getComponent<engine::SkeletalMeshComponent>();
                engine::Skeleton *skeleton = skeletalMeshComponent ? &skeletalMeshComponent->getSkeleton() : nullptr;
                const std::string selectedBoneName = editor.getSelectedSkeletalBoneName();
                auto &profile = ragdollComponent->getProfile();

                const char *stateLabel = "Inactive";
                switch (ragdollComponent->getRuntimeState())
                {
                case engine::RagdollComponent::RuntimeState::Inactive:
                    stateLabel = "Inactive";
                    break;
                case engine::RagdollComponent::RuntimeState::Simulating:
                    stateLabel = "Simulating";
                    break;
                case engine::RagdollComponent::RuntimeState::Recovering:
                    stateLabel = "Recovering";
                    break;
                }

                ImGui::Text("State: %s", stateLabel);
                ImGui::Text("Bodies: %zu  Joints: %zu", profile.bodies.size(), profile.joints.size());
                if (!selectedBoneName.empty())
                    ImGui::TextDisabled("Selected Bone: %s", selectedBoneName.c_str());

                if (ImGui::Button("Auto Generate Humanoid"))
                    ragdollComponent->autoGenerateHumanoidProfile();
                ImGui::SameLine();
                if (ImGui::Button("Rebuild Profile"))
                    ragdollComponent->buildFromProfile();

                bool debugDrawBodies = ragdollComponent->getDebugDrawBodies();
                if (ImGui::Checkbox("Debug Draw Bodies", &debugDrawBodies))
                    ragdollComponent->setDebugDrawBodies(debugDrawBodies);
                bool debugDrawJoints = ragdollComponent->getDebugDrawJoints();
                if (ImGui::Checkbox("Debug Draw Joints", &debugDrawJoints))
                    ragdollComponent->setDebugDrawJoints(debugDrawJoints);

                drawBoneCombo("Reference Bone", skeleton, profile.referenceBoneName);
                if (!selectedBoneName.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Use Selected##RagdollReference"))
                        profile.referenceBoneName = selectedBoneName;
                }

                if (ImGui::Button("Enter Ragdoll"))
                    ragdollComponent->enterRagdoll(true);
                ImGui::SameLine();
                if (ImGui::Button("Exit Ragdoll"))
                    ragdollComponent->exitRagdoll(0.25f);

                ImGui::Separator();
                ImGui::TextUnformatted("Bodies");
                if (ImGui::Button("Add Body"))
                    profile.bodies.emplace_back();
                if (!selectedBoneName.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Add Selected Bone Body"))
                    {
                        engine::RagdollBodyDesc body{};
                        body.boneName = selectedBoneName;
                        profile.bodies.push_back(body);
                    }
                }

                int bodyIndexToRemove = -1;
                for (size_t bodyIndex = 0; bodyIndex < profile.bodies.size(); ++bodyIndex)
                {
                    auto &body = profile.bodies[bodyIndex];
                    ImGui::PushID(static_cast<int>(bodyIndex));
                    const std::string treeLabel = body.boneName.empty()
                                                      ? ("Body " + std::to_string(bodyIndex))
                                                      : body.boneName + "##RagdollBody";
                    if (ImGui::TreeNodeEx(treeLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        drawBoneCombo("Bone", skeleton, body.boneName);
                        if (!selectedBoneName.empty())
                        {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Use Selected##RagdollBodyBone"))
                                body.boneName = selectedBoneName;
                        }

                        int shapeType = static_cast<int>(body.shapeType);
                        if (ImGui::Combo("Shape", &shapeType, "Box\0Capsule\0"))
                            body.shapeType = static_cast<engine::RagdollBodyShapeType>(shapeType);

                        if (body.shapeType == engine::RagdollBodyShapeType::Box)
                        {
                            ImGui::DragFloat3("Box Half Extents", &body.boxHalfExtents.x, 0.005f, 0.01f, 10.0f, "%.3f");
                        }
                        else
                        {
                            ImGui::DragFloat("Capsule Radius", &body.capsuleRadius, 0.005f, 0.01f, 10.0f, "%.3f");
                            ImGui::DragFloat("Capsule Half Height", &body.capsuleHalfHeight, 0.005f, 0.0f, 10.0f, "%.3f");
                        }

                        ImGui::DragFloat3("Body Local Position", &body.bodyLocalPosition.x, 0.005f, -10.0f, 10.0f, "%.3f");
                        ImGui::DragFloat3("Body Local Euler", &body.bodyLocalEulerDegrees.x, 0.25f, -180.0f, 180.0f, "%.1f");
                        ImGui::DragFloat("Mass", &body.mass, 0.01f, 0.01f, 100.0f, "%.2f");
                        ImGui::DragFloat("Linear Damping", &body.linearDamping, 0.005f, 0.0f, 10.0f, "%.3f");
                        ImGui::DragFloat("Angular Damping", &body.angularDamping, 0.005f, 0.0f, 10.0f, "%.3f");

                        if (ImGui::Button("Remove Body"))
                            bodyIndexToRemove = static_cast<int>(bodyIndex);

                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                if (bodyIndexToRemove >= 0)
                    profile.bodies.erase(profile.bodies.begin() + bodyIndexToRemove);

                ImGui::Separator();
                ImGui::TextUnformatted("Joints");
                if (ImGui::Button("Add Joint"))
                    profile.joints.emplace_back();
                if (!selectedBoneName.empty() && !profile.joints.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("Use selected bone buttons inside a joint to assign.");
                }

                int jointIndexToRemove = -1;
                for (size_t jointIndex = 0; jointIndex < profile.joints.size(); ++jointIndex)
                {
                    auto &joint = profile.joints[jointIndex];
                    ImGui::PushID(static_cast<int>(jointIndex));
                    const std::string treeLabel =
                        (joint.childBoneName.empty() ? ("Joint " + std::to_string(jointIndex)) : joint.childBoneName) + "##RagdollJoint";
                    if (ImGui::TreeNodeEx(treeLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        drawBoneCombo("Parent Bone", skeleton, joint.parentBoneName);
                        if (!selectedBoneName.empty())
                        {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Use Selected##RagdollJointParent"))
                                joint.parentBoneName = selectedBoneName;
                        }

                        drawBoneCombo("Child Bone", skeleton, joint.childBoneName);
                        if (!selectedBoneName.empty())
                        {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Use Selected##RagdollJointChild"))
                                joint.childBoneName = selectedBoneName;
                        }

                        ImGui::DragFloat("Swing Y Limit", &joint.swingYLimitDeg, 0.5f, 0.0f, 179.0f, "%.1f");
                        ImGui::DragFloat("Swing Z Limit", &joint.swingZLimitDeg, 0.5f, 0.0f, 179.0f, "%.1f");
                        ImGui::DragFloat("Twist Lower", &joint.twistLowerLimitDeg, 0.5f, -179.0f, 179.0f, "%.1f");
                        ImGui::DragFloat("Twist Upper", &joint.twistUpperLimitDeg, 0.5f, -179.0f, 179.0f, "%.1f");
                        ImGui::Checkbox("Enable Collision", &joint.collisionEnabled);

                        if (ImGui::Button("Remove Joint"))
                            jointIndexToRemove = static_cast<int>(jointIndex);

                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                if (jointIndexToRemove >= 0)
                    profile.joints.erase(profile.joints.begin() + jointIndexToRemove);

                ImGui::Separator();
                ImGui::TextDisabled("Profile edits apply after Rebuild Profile or the next Enter Ragdoll.");
                if (!skeleton || skeleton->getBonesCount() == 0)
                    ImGui::TextDisabled("Skeleton data is not ready yet. The ragdoll profile will build after the model loads.");

                if (ImGui::Button("Remove Ragdoll"))
                {
                    m_selectedEntity->removeComponent<engine::RagdollComponent>();
                    return;
                }
            }
        }
        else if (auto cameraComponent = dynamic_cast<engine::CameraComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
            {
                drawComponentClipboardToolbar(m_notificationManager, m_scene.get(), m_selectedEntity, cameraComponent, "Camera");
                auto camera = cameraComponent->getCamera();
                float yaw = camera->getYaw();
                float pitch = camera->getPitch();
                float fov = camera->getFOV();

                if (ImGui::DragFloat("Yaw", &yaw))
                    camera->setYaw(yaw);

                if (ImGui::DragFloat("Pitch", &pitch))
                    camera->setPitch(pitch);

                if (ImGui::DragFloat("FOV", &fov))
                    camera->setFOV(fov);

                glm::vec3 cameraOffset = cameraComponent->getPositionOffset();
                if (ImGui::DragFloat3("Position offset", &cameraOffset.x, 0.01f))
                    cameraComponent->setPositionOffset(cameraOffset);

                if (auto *transformComponent = m_selectedEntity->getComponent<engine::Transform3DComponent>())
                {
                    const glm::vec3 entityWorldPosition = transformComponent->getWorldPosition();
                    ImGui::Text("Entity position: %.2f %.2f %.2f", entityWorldPosition.x, entityWorldPosition.y, entityWorldPosition.z);
                }

                const glm::vec3 cameraWorldPosition = camera->getPosition();
                ImGui::Text("Camera position: %.2f %.2f %.2f", cameraWorldPosition.x, cameraWorldPosition.y, cameraWorldPosition.z);
            }
        }
        else if (auto rigidBodyComponent = dynamic_cast<engine::RigidBodyComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("RigidBody", ImGuiTreeNodeFlags_DefaultOpen))
            {
                bool isKinematic = true;

                if (ImGui::Checkbox("Kinematic", &isKinematic))
                {
                    rigidBodyComponent->setKinematic(isKinematic);
                }
            }
        }
        else if (auto characterMovement = dynamic_cast<engine::CharacterMovementComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Character Movement", ImGuiTreeNodeFlags_DefaultOpen))
            {
                drawComponentClipboardToolbar(m_notificationManager, m_scene.get(), m_selectedEntity, characterMovement, "Character Movement");
                float radius = characterMovement->getCapsuleRadius();
                float height = characterMovement->getCapsuleHeight();
                float centerOffsetY = characterMovement->getCapsuleCenterOffsetY();
                bool capsuleChanged = false;
                capsuleChanged |= ImGui::DragFloat("Capsule Radius", &radius, 0.01f, 0.05f, 1000.0f, "%.3f");
                capsuleChanged |= ImGui::DragFloat("Capsule Height", &height, 0.01f, 0.1f, 1000.0f, "%.3f");
                if (capsuleChanged)
                    characterMovement->setCapsule(radius, height);

                if (ImGui::DragFloat("Capsule Center Offset Y", &centerOffsetY, 0.01f, -1000.0f, 1000.0f, "%.3f"))
                    characterMovement->setCapsuleCenterOffsetY(centerOffsetY);

                const float capsuleBottomLocalY = centerOffsetY - (height * 0.5f + radius);
                ImGui::TextDisabled("Capsule Bottom (local Y): %.3f", capsuleBottomLocalY);
                if (auto *transformComponent = m_selectedEntity->getComponent<engine::Transform3DComponent>())
                {
                    const float capsuleBottomWorldY = transformComponent->getWorldPosition().y + capsuleBottomLocalY;
                    ImGui::TextDisabled("Capsule Bottom (world Y): %.3f", capsuleBottomWorldY);
                }

                float stepOffset = characterMovement->getStepOffset();
                if (ImGui::DragFloat("Step Offset", &stepOffset, 0.01f, 0.0f, 1000.0f, "%.3f"))
                    characterMovement->setStepOffset(stepOffset);

                float contactOffset = characterMovement->getContactOffset();
                if (ImGui::DragFloat("Contact Offset", &contactOffset, 0.001f, 0.001f, 1.0f, "%.3f"))
                    characterMovement->setContactOffset(contactOffset);

                float slopeLimit = characterMovement->getSlopeLimitDegrees();
                if (ImGui::DragFloat("Slope Limit (deg)", &slopeLimit, 0.5f, 0.0f, 89.0f, "%.1f"))
                    characterMovement->setSlopeLimitDegrees(slopeLimit);

                ImGui::Text("Grounded: %s", characterMovement->isGrounded() ? "Yes" : "No");
                ImGui::Text("Collision flags: %u", characterMovement->getCollisionFlags());

                if (ImGui::Button("Remove Character Movement"))
                {
                    m_selectedEntity->removeComponent<engine::CharacterMovementComponent>();
                    return;
                }
            }
        }
        else if (auto collisionComponent = dynamic_cast<engine::CollisionComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Collision", ImGuiTreeNodeFlags_DefaultOpen))
            {
                drawComponentClipboardToolbar(m_notificationManager, m_scene.get(), m_selectedEntity, collisionComponent, "Collision");
                const bool isBox = collisionComponent->getShapeType() == engine::CollisionComponent::ShapeType::BOX;
                ImGui::Text("Type: %s", isBox ? "Box" : "Capsule");

                ImGui::Checkbox("Show Bounds", &m_showCollisionBounds);
                ImGui::Checkbox("Editable Bounds", &m_enableCollisionBoundsEditing);

                if (isBox)
                {
                    glm::vec3 halfExtents = collisionComponent->getBoxHalfExtents();
                    if (ImGui::DragFloat3("Half Extents", &halfExtents.x, 0.01f, 0.01f, 1000.0f, "%.3f"))
                        collisionComponent->setBoxHalfExtents(halfExtents);
                }
                else
                {
                    float capsuleRadius = collisionComponent->getCapsuleRadius();
                    float capsuleHalfHeight = collisionComponent->getCapsuleHalfHeight();

                    bool geometryChanged = false;
                    geometryChanged |= ImGui::DragFloat("Radius", &capsuleRadius, 0.01f, 0.01f, 1000.0f, "%.3f");
                    geometryChanged |= ImGui::DragFloat("Half Height", &capsuleHalfHeight, 0.01f, 0.0f, 1000.0f, "%.3f");

                    if (geometryChanged)
                        collisionComponent->setCapsuleDimensions(capsuleRadius, capsuleHalfHeight);
                }

                if (ImGui::Button("Remove Collision"))
                {
                    destroyCollisionComponent(m_scene.get(), m_selectedEntity, collisionComponent);
                    return;
                }
            }
        }
        else if (auto audioComponent = dynamic_cast<engine::AudioComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const std::string &assetPath = audioComponent->getAssetPath();
                const bool hasAudio = !assetPath.empty();

                ImGui::PushID("AudioAssetDrop");

                const std::string assetLabel = hasAudio
                                                   ? std::filesystem::path(assetPath).filename().string()
                                                   : "<None>";

                ImGui::TextUnformatted("Audio Asset:");
                ImGui::SameLine();
                ImGui::TextDisabled("%s", assetLabel.c_str());

                ImGui::Button("Drop .audio.elixasset here##AudioDrop");
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                    {
                        std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                        std::string extension = std::filesystem::path(droppedPath).extension().string();
                        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c)
                                       { return static_cast<char>(std::tolower(c)); });
                        if (extension == ".elixasset")
                        {
                            if (audioComponent->loadFromAsset(droppedPath))
                                m_notificationManager.showSuccess("Audio asset loaded");
                            else
                                m_notificationManager.showError("Failed to load audio asset");
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (hasAudio && ImGui::Button("Clear##AudioClear"))
                {
                    audioComponent->clearAudio();
                    m_notificationManager.showSuccess("Audio cleared");
                }

                ImGui::PopID();

                ImGui::Separator();

                bool playOnStart = audioComponent->isPlayOnStart();
                if (ImGui::Checkbox("Play On Start", &playOnStart))
                    audioComponent->setPlayOnStart(playOnStart);

                bool loop = audioComponent->isLooping();
                if (ImGui::Checkbox("Loop", &loop))
                    audioComponent->setLooping(loop);

                bool muted = audioComponent->isMuted();
                if (ImGui::Checkbox("Mute", &muted))
                    audioComponent->setMuted(muted);

                bool spatial = audioComponent->isSpatial();
                if (ImGui::Checkbox("Spatial (3D)", &spatial))
                    audioComponent->setSpatial(spatial);

                float volume = audioComponent->getVolume();
                if (ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f, "%.2f"))
                    audioComponent->setVolume(volume);

                float pitch = audioComponent->getPitch();
                if (ImGui::SliderFloat("Pitch", &pitch, 0.1f, 4.0f, "%.2f"))
                    audioComponent->setPitch(pitch);

                if (spatial)
                {
                    float minDist = audioComponent->getMinDistance();
                    if (ImGui::DragFloat("Min Distance", &minDist, 0.1f, 0.0f, 10000.0f, "%.1f"))
                        audioComponent->setMinDistance(minDist);

                    float maxDist = audioComponent->getMaxDistance();
                    if (ImGui::DragFloat("Max Distance", &maxDist, 1.0f, 0.0f, 10000.0f, "%.1f"))
                        audioComponent->setMaxDistance(maxDist);
                }

                ImGui::Separator();

                const bool isPlaying = audioComponent->isPlaying();
                const bool isPaused = audioComponent->isPaused();

                if (!isPlaying && !isPaused)
                {
                    if (ImGui::Button("Play") && hasAudio)
                        audioComponent->play();
                }
                else if (isPaused)
                {
                    if (ImGui::Button("Resume"))
                        audioComponent->resume();
                    ImGui::SameLine();
                    if (ImGui::Button("Stop"))
                        audioComponent->stop();
                }
                else
                {
                    if (ImGui::Button("Pause"))
                        audioComponent->pause();
                    ImGui::SameLine();
                    if (ImGui::Button("Stop"))
                        audioComponent->stop();
                }

                ImGui::SameLine();
                const char *statusText = isPlaying ? "Playing" : (isPaused ? "Paused" : "Stopped");
                ImGui::TextDisabled("[%s]", statusText);

                ImGui::Separator();

                if (ImGui::Button("Remove Audio Source"))
                {
                    m_selectedEntity->removeComponent<engine::AudioComponent>();
                    return;
                }
            }
        }
        else if (auto *probeComponent = dynamic_cast<engine::ReflectionProbeComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Reflection Probe", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushID("ReflectionProbeComp");

                const std::string &currentHDR = probeComponent->hdrPath;
                const bool hasHDR = !currentHDR.empty();

                ImGui::TextUnformatted("HDR Environment:");
                ImGui::SameLine();
                ImGui::TextDisabled("%s", hasHDR
                                              ? std::filesystem::path(currentHDR).filename().string().c_str()
                                              : "<None>");

                ImGui::Button("Drop .hdr file here##ProbeDrop");
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                    {
                        std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                        std::string ext = std::filesystem::path(droppedPath).extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                                       { return static_cast<char>(std::tolower(c)); });
                        if (ext == ".hdr" || ext == ".elixasset")
                        {
                            auto *pool = core::VulkanContext::getContext()->getPersistentDescriptorPool()->vk();
                            probeComponent->setHDRPath(droppedPath, pool);
                            if (probeComponent->hasCubemap())
                                m_notificationManager.showSuccess("Reflection probe HDR loaded");
                            else
                                m_notificationManager.showError("Failed to load reflection probe HDR");
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (hasHDR && ImGui::Button("Clear##ProbeClear"))
                {
                    probeComponent->hdrPath.clear();
                    probeComponent->reload(core::VulkanContext::getContext()->getPersistentDescriptorPool()->vk());
                }

                ImGui::Separator();

                ImGui::DragFloat("Radius##ProbeRadius", &probeComponent->radius, 0.1f, 0.1f, 500.0f, "%.1f");
                ImGui::DragFloat("Intensity##ProbeIntens", &probeComponent->intensity, 0.01f, 0.0f, 10.0f, "%.2f");

                ImGui::Separator();

                const char *statusStr = probeComponent->hasCapturedScene()
                                            ? "Scene Captured"
                                            : (probeComponent->hasCubemap() ? "HDR Loaded" : "Not Captured");
                ImGui::TextDisabled("Status: %s", statusStr);

                ImGui::Spacing();

                if (ImGui::Button("Capture Scene##ProbeCapture"))
                    editor.requestProbeCapture(m_selectedEntity);

                ImGui::SameLine();
                ImGui::TextDisabled("(renders scene from probe position)");

                ImGui::Separator();

                if (ImGui::Button("Remove Reflection Probe"))
                {
                    m_selectedEntity->removeComponent<engine::ReflectionProbeComponent>();
                    ImGui::PopID();
                    return;
                }

                ImGui::PopID();
            }
        }
        else if (auto *spriteComponent = dynamic_cast<engine::SpriteComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushID("SpriteComp");

                // Texture slot
                const std::string texLabel = spriteComponent->texturePath.empty()
                    ? std::string("<None>")
                    : std::filesystem::path(spriteComponent->texturePath).filename().string();
                ImGui::TextUnformatted("Texture:");
                ImGui::SameLine();
                ImGui::TextDisabled("%s", texLabel.c_str());

                ImGui::Button("Drop .elixasset here##SpriteTex");
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                    {
                        std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                        spriteComponent->texturePath = droppedPath;
                    }
                    ImGui::EndDragDropTarget();
                }
                if (!spriteComponent->texturePath.empty() && ImGui::Button("Clear##SpriteClearTex"))
                    spriteComponent->texturePath.clear();

                ImGui::Separator();

                ImGui::ColorEdit4("Color##SpriteColor", &spriteComponent->color.x);
                ImGui::DragFloat2("Size##SpriteSize", &spriteComponent->size.x, 0.01f, 0.001f, 1000.f, "%.3f");
                ImGui::DragFloat("Rotation##SpriteRot", &spriteComponent->rotation, 0.01f, -6.2832f, 6.2832f, "%.3f rad");
                ImGui::DragInt("Sort Layer##SpriteSortLayer", &spriteComponent->sortLayer);

                ImGui::Separator();

                ImGui::TextUnformatted("UV Rect (u0, v0, u1, v1):");
                ImGui::DragFloat4("##SpriteUVRect", &spriteComponent->uvRect.x, 0.005f, 0.f, 1.f, "%.3f");

                ImGui::Checkbox("Flip X##SpriteFlipX", &spriteComponent->flipX);
                ImGui::SameLine();
                ImGui::Checkbox("Flip Y##SpriteFlipY", &spriteComponent->flipY);
                ImGui::Checkbox("Visible##SpriteVisible", &spriteComponent->visible);

                ImGui::Separator();

                if (ImGui::Button("Remove Sprite"))
                {
                    m_selectedEntity->removeComponent<engine::SpriteComponent>();
                    ImGui::PopID();
                    return;
                }

                ImGui::PopID();
            }
        }
        else if (auto *decalComponent = dynamic_cast<engine::DecalComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Decal", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushID("DecalComp");

                const bool hasMaterial = decalComponent->material != nullptr;
                const bool hasMaterialBinding = hasMaterial || !decalComponent->materialPath.empty();
                const std::string matLabel =
                    !decalComponent->materialPath.empty()
                        ? std::filesystem::path(decalComponent->materialPath).filename().string()
                        : (hasMaterial ? std::string("<Loaded Material>") : std::string("<NONE>"));

                ImGui::TextUnformatted("Material:");
                ImGui::SameLine();
                ImGui::TextDisabled("%s", matLabel.c_str());

                if (hasMaterial && decalComponent->material->getDomain() != engine::MaterialDomain::DeferredDecal)
                    ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "Material domain must be Deferred Decal to render.");

                ImGui::Button("Drop .elixmat here##DecalMatDrop");
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                    {
                        std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                        std::string extension = std::filesystem::path(droppedPath).extension().string();
                        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c)
                                       { return static_cast<char>(std::tolower(c)); });
                        if (extension == ".elixmat")
                        {
                            auto mat = editor.ensureMaterialLoaded(droppedPath);
                            if (mat)
                            {
                                decalComponent->material = mat;
                                decalComponent->materialPath = droppedPath;
                                if (mat->getDomain() == engine::MaterialDomain::DeferredDecal)
                                    m_notificationManager.showSuccess("Decal material assigned");
                                else
                                    m_notificationManager.showWarning("Material assigned, but Domain must be Deferred Decal to render");
                            }
                            else
                            {
                                m_notificationManager.showError("Failed to load decal material");
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (hasMaterialBinding && ImGui::Button("Clear Material##DecalClear"))
                {
                    decalComponent->material = nullptr;
                    decalComponent->materialPath.clear();
                }

                ImGui::Separator();

                ImGui::DragFloat3("Half Extents##DecalSize", &decalComponent->size.x, 0.05f, 0.01f, 500.0f, "%.2f");
                ImGui::SliderFloat("Opacity##DecalOpacity", &decalComponent->opacity, 0.0f, 1.0f, "%.2f");
                ImGui::DragInt("Sort Order##DecalSort", &decalComponent->sortOrder, 1.0f, -100, 100);

                ImGui::Separator();

                if (ImGui::Button("Remove Decal"))
                {
                    m_selectedEntity->removeComponent<engine::DecalComponent>();
                    ImGui::PopID();
                    return;
                }

                ImGui::PopID();
            }
        }
    }

    const auto scriptComponents = m_selectedEntity->getComponents<engine::ScriptComponent>();

    if (!scriptComponents.empty() && ImGui::CollapsingHeader("Scripts", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (size_t scriptIndex = 0; scriptIndex < scriptComponents.size(); ++scriptIndex)
        {
            auto *scriptComponent = scriptComponents[scriptIndex];
            const std::string &scriptName = scriptComponent->getScriptName();
            const std::string displayName = scriptName.empty() ? std::string("<Unnamed Script>") : scriptName;

            ImGui::PushID(static_cast<int>(scriptIndex));

            if (ImGui::TreeNodeEx(displayName.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                drawComponentClipboardToolbar(m_notificationManager, m_scene.get(), m_selectedEntity, scriptComponent, displayName.c_str());
                auto *scriptInstance = scriptComponent->getScript();
                if (!scriptInstance)
                {
                    if (scriptComponent->isBroken())
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                        ImGui::TextWrapped("Plugin not loaded — script data preserved.");
                        ImGui::PopStyleColor();
                        ImGui::TextDisabled("Load the plugin and reload the scene to restore.");
                    }
                    else
                    {
                        ImGui::TextDisabled("Script instance is null");
                    }
                }
                else
                {
                    const auto &variables = scriptInstance->getExposedVariables();
                    if (variables.empty())
                    {
                        ImGui::TextDisabled("No exposed variables");
                        ImGui::TextDisabled("Use Script::exposeBool/Int/Float/String/Vec2/Vec3/Vec4/Entity");
                    }
                    else
                    {
                        std::vector<std::string> variableNames;
                        variableNames.reserve(variables.size());
                        for (const auto &[variableName, _] : variables)
                            variableNames.push_back(variableName);

                        std::sort(variableNames.begin(), variableNames.end());

                        for (const auto &variableName : variableNames)
                        {
                            const auto variableIt = variables.find(variableName);
                            if (variableIt == variables.end())
                                continue;

                            const auto &variable = variableIt->second;
                            using ExposedVariableType = engine::Script::ExposedVariableType;

                            switch (variable.type)
                            {
                            case ExposedVariableType::Bool:
                            {
                                bool value = std::get<bool>(variable.value);
                                if (ImGui::Checkbox(variableName.c_str(), &value))
                                    scriptInstance->setExposedVariable(variableName, value);
                                break;
                            }
                            case ExposedVariableType::Int:
                            {
                                int value = std::get<int32_t>(variable.value);
                                if (ImGui::DragInt(variableName.c_str(), &value, 1.0f))
                                    scriptInstance->setExposedVariable(variableName, static_cast<int32_t>(value));
                                break;
                            }
                            case ExposedVariableType::Float:
                            {
                                float value = std::get<float>(variable.value);
                                if (ImGui::DragFloat(variableName.c_str(), &value, 0.05f))
                                    scriptInstance->setExposedVariable(variableName, value);
                                break;
                            }
                            case ExposedVariableType::String:
                            {
                                const auto &stringValue = std::get<std::string>(variable.value);
                                std::array<char, 512> buffer{};
                                std::strncpy(buffer.data(), stringValue.c_str(), buffer.size() - 1);
                                if (ImGui::InputText(variableName.c_str(), buffer.data(), buffer.size()))
                                    scriptInstance->setExposedVariable(variableName, std::string(buffer.data()));
                                break;
                            }
                            case ExposedVariableType::Vec2:
                            {
                                glm::vec2 value = std::get<glm::vec2>(variable.value);
                                if (ImGui::DragFloat2(variableName.c_str(), glm::value_ptr(value), 0.05f))
                                    scriptInstance->setExposedVariable(variableName, value);
                                break;
                            }
                            case ExposedVariableType::Vec3:
                            {
                                glm::vec3 value = std::get<glm::vec3>(variable.value);
                                if (ImGui::DragFloat3(variableName.c_str(), glm::value_ptr(value), 0.05f))
                                    scriptInstance->setExposedVariable(variableName, value);
                                break;
                            }
                            case ExposedVariableType::Vec4:
                            {
                                glm::vec4 value = std::get<glm::vec4>(variable.value);
                                if (ImGui::DragFloat4(variableName.c_str(), glm::value_ptr(value), 0.05f))
                                    scriptInstance->setExposedVariable(variableName, value);
                                break;
                            }
                            case ExposedVariableType::Entity:
                            {
                                auto selectedRef = std::get<engine::Script::EntityRef>(variable.value);
                                std::string previewValue = "<None>";

                                if (selectedRef.isValid())
                                {
                                    if (m_scene)
                                    {
                                        if (auto *selectedEntity = m_scene->getEntityById(selectedRef.id))
                                            previewValue = selectedEntity->getName() + " (" + std::to_string(selectedRef.id) + ")";
                                        else
                                            previewValue = "<Missing: " + std::to_string(selectedRef.id) + ">";
                                    }
                                    else
                                        previewValue = "<Entity id: " + std::to_string(selectedRef.id) + ">";
                                }

                                if (ImGui::BeginCombo(variableName.c_str(), previewValue.c_str()))
                                {
                                    const bool isNoneSelected = !selectedRef.isValid();
                                    if (ImGui::Selectable("<None>", isNoneSelected))
                                        scriptInstance->setExposedVariable(variableName, engine::Script::EntityRef{});

                                    if (isNoneSelected)
                                        ImGui::SetItemDefaultFocus();

                                    if (m_scene)
                                    {
                                        for (const auto &candidateEntity : m_scene->getEntities())
                                        {
                                            if (!candidateEntity)
                                                continue;

                                            const auto candidateId = candidateEntity->getId();
                                            const bool isCurrent = selectedRef.isValid() && selectedRef.id == candidateId;
                                            const std::string label = candidateEntity->getName() + " (" + std::to_string(candidateId) + ")";

                                            if (ImGui::Selectable(label.c_str(), isCurrent))
                                                scriptInstance->setExposedVariable(variableName, engine::Script::EntityRef(candidateId));

                                            if (isCurrent)
                                                ImGui::SetItemDefaultFocus();
                                        }
                                    }

                                    ImGui::EndCombo();
                                }

                                break;
                            }
                            default:
                                break;
                            }
                        }
                    }
                }

                scriptComponent->syncSerializedVariablesFromScript();
                ImGui::TreePop();
            }

            ImGui::PopID();
        }
    }

    const auto particleSystemComponents = m_selectedEntity->getComponents<engine::ParticleSystemComponent>();

    for (size_t psIdx = 0; psIdx < particleSystemComponents.size(); ++psIdx)
    {
        auto *psComp = particleSystemComponents[psIdx];
        auto *ps = psComp->getParticleSystem();

        ImGui::PushID(static_cast<int>(psIdx));

        const std::string psHeader = std::string("Particle System: ") + (ps ? ps->name : "<No System>");
        if (ImGui::CollapsingHeader(psHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Play On Start", &psComp->playOnStart);

            // Drag-drop zone: drop .vfx.elixasset to load particle system from asset
            ImGui::Button(psComp->vfxAssetPath.empty() ? "Drop .vfx.elixasset here##VFXDrop" : (std::string("VFX Asset: ") + std::filesystem::path(psComp->vfxAssetPath).filename().string()).c_str());
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                {
                    const std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                    const std::string lowerPath = [&droppedPath]()
                    {
                        std::string s = droppedPath;
                        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        return s;
                    }();
                    constexpr const char *vfxSuffix = ".vfx.elixasset";
                    if (lowerPath.size() > std::strlen(vfxSuffix) &&
                        lowerPath.rfind(vfxSuffix) == lowerPath.size() - std::strlen(vfxSuffix))
                    {
                        if (psComp->loadFromAsset(droppedPath))
                        {
                            ps = psComp->getParticleSystem();
                            m_notificationManager.showSuccess("VFX asset loaded");
                        }
                        else
                            m_notificationManager.showError("Failed to load VFX asset");
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (!ps)
            {
                if (ImGui::Button("Create Empty System"))
                {
                    auto newSys = std::make_shared<engine::ParticleSystem>();
                    newSys->name = "Particle System";
                    psComp->setParticleSystem(newSys);
                    ps = psComp->getParticleSystem();
                }
            }
            else
            {
                char sysNameBuf[128];
                std::strncpy(sysNameBuf, ps->name.c_str(), sizeof(sysNameBuf));
                if (ImGui::InputText("Name##SysName", sysNameBuf, sizeof(sysNameBuf)))
                    ps->name = sysNameBuf;

                if (ImGui::Button("Save as VFX Asset"))
                {
                    const std::string savePath = psComp->vfxAssetPath.empty()
                        ? (std::string("assets/") + ps->name + ".vfx.elixasset")
                        : psComp->vfxAssetPath;
                    if (psComp->saveToAsset(savePath))
                    {
                        psComp->vfxAssetPath = savePath;
                        m_notificationManager.showSuccess("VFX asset saved");
                    }
                    else
                        m_notificationManager.showError("Failed to save VFX asset");
                }

                ImGui::Separator();
                const bool sysPlaying = ps->isPlaying();
                const bool sysPaused = ps->isPaused();

                if (!sysPlaying && !sysPaused)
                {
                    if (ImGui::Button("Play"))
                        psComp->play();
                }
                else if (sysPaused)
                {
                    if (ImGui::Button("Resume"))
                        psComp->play();
                    ImGui::SameLine();
                    if (ImGui::Button("Stop"))
                        psComp->stop();
                }
                else
                {
                    if (ImGui::Button("Pause"))
                        psComp->pause();
                    ImGui::SameLine();
                    if (ImGui::Button("Stop"))
                        psComp->stop();
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset"))
                    psComp->reset();
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", sysPlaying ? "Playing" : (sysPaused ? "Paused" : "Stopped"));

                ImGui::Separator();
                if (ImGui::Button("+ Add Emitter"))
                    ps->addEmitter("Emitter " + std::to_string(ps->getEmitters().size()));
                ImGui::Separator();

                std::string emitterToRemove;
                const auto &emitters = ps->getEmitters();

                for (size_t emIdx = 0; emIdx < emitters.size(); ++emIdx)
                {
                    auto *em = emitters[emIdx].get();
                    ImGui::PushID(static_cast<int>(emIdx));

                    const std::string emHeader = std::string("Emitter: ") + em->name;
                    if (ImGui::CollapsingHeader(emHeader.c_str()))
                    {
                        char emNameBuf[128];
                        std::strncpy(emNameBuf, em->name.c_str(), sizeof(emNameBuf));
                        if (ImGui::InputText("##EmName", emNameBuf, sizeof(emNameBuf)))
                            em->name = emNameBuf;
                        ImGui::SameLine();
                        ImGui::Checkbox("Enabled##Em", &em->enabled);
                        ImGui::TextDisabled("Alive: %u / %u", em->getAliveCount(), engine::ParticleEmitter::MAX_PARTICLES);

                        // SpawnModule
                        if (auto *spawn = em->getModule<engine::SpawnModule>())
                        {
                            if (ImGui::TreeNodeEx("Spawn##SpawnMod", ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                ImGui::DragFloat("Rate (p/s)", &spawn->spawnRate, 1.0f, 0.0f, 100000.0f);
                                ImGui::DragFloat("Burst Count", &spawn->burstCount, 1.0f, 0.0f, 100000.0f);
                                ImGui::Checkbox("Loop", &spawn->loop);
                                if (!spawn->loop)
                                    ImGui::DragFloat("Duration (s)", &spawn->duration, 0.01f, 0.01f, 1000.0f);

                                static const char *shapeNames[] = {"Point", "Sphere", "Box", "Cone", "Cylinder"};
                                int shapeIdx = static_cast<int>(spawn->shape.shape);
                                if (ImGui::Combo("Shape", &shapeIdx, shapeNames, 5))
                                    spawn->shape.shape = static_cast<engine::EmitterShape>(shapeIdx);

                                using ES = engine::EmitterShape;
                                if (spawn->shape.shape != ES::Point)
                                {
                                    if (spawn->shape.shape == ES::Box)
                                        ImGui::DragFloat3("Extents", &spawn->shape.extents.x, 0.1f, 0.0f, 10000.0f);
                                    else
                                        ImGui::DragFloat("Radius", &spawn->shape.radius, 0.1f, 0.0f, 10000.0f);

                                    if (spawn->shape.shape == ES::Cone || spawn->shape.shape == ES::Cylinder)
                                    {
                                        ImGui::DragFloat("Height", &spawn->shape.height, 0.1f, 0.0f, 10000.0f);
                                        if (spawn->shape.shape == ES::Cone)
                                            ImGui::DragFloat("Half Angle (deg)", &spawn->shape.angle, 0.5f, 0.0f, 90.0f);
                                    }
                                    ImGui::Checkbox("Surface Only", &spawn->shape.surfaceOnly);
                                }

                                // Sub-emitter on death
                                ImGui::Separator();
                                ImGui::TextUnformatted("Sub-Emitter on Death:");
                                char subEmitterBuf[128] = {};
                                std::strncpy(subEmitterBuf, spawn->subEmitterOnDeath.c_str(), sizeof(subEmitterBuf) - 1);
                                if (ImGui::InputText("Target Emitter##SubEm", subEmitterBuf, sizeof(subEmitterBuf)))
                                    spawn->subEmitterOnDeath = subEmitterBuf;
                                ImGui::DragInt("Burst Count##SubEmBurst", &spawn->subEmitterBurstCount, 1, 1, 1000);

                                ImGui::TreePop();
                            }
                        }
                        else if (ImGui::SmallButton("+ Spawn"))
                            em->addModule<engine::SpawnModule>();

                        // LifetimeModule
                        if (auto *lt = em->getModule<engine::LifetimeModule>())
                        {
                            if (ImGui::TreeNodeEx("Lifetime##LTMod", ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                ImGui::DragFloat("Min (s)", &lt->minLifetime, 0.01f, 0.01f, 1000.0f);
                                ImGui::DragFloat("Max (s)", &lt->maxLifetime, 0.01f, 0.01f, 1000.0f);
                                ImGui::TreePop();
                            }
                        }
                        else if (ImGui::SmallButton("+ Lifetime"))
                            em->addModule<engine::LifetimeModule>();

                        // InitialVelocityModule
                        if (auto *vel = em->getModule<engine::InitialVelocityModule>())
                        {
                            if (ImGui::TreeNodeEx("Initial Velocity##VelMod", ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                ImGui::DragFloat3("Base Velocity", &vel->baseVelocity.x, 0.1f);
                                ImGui::DragFloat3("Randomness", &vel->randomness.x, 0.1f, 0.0f, 1000.0f);
                                ImGui::TreePop();
                            }
                        }
                        else if (ImGui::SmallButton("+ Velocity"))
                            em->addModule<engine::InitialVelocityModule>();

                        // ForceModule
                        if (auto *force = em->getModule<engine::ForceModule>())
                        {
                            if (ImGui::TreeNodeEx("Force##ForceMod", ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                ImGui::DragFloat3("Force (m/s²)", &force->force.x, 0.1f);
                                ImGui::DragFloat("Drag", &force->drag, 0.001f, 0.0f, 100.0f);
                                ImGui::TreePop();
                            }
                        }
                        else if (ImGui::SmallButton("+ Force"))
                            em->addModule<engine::ForceModule>();

                        // SizeOverLifetimeModule
                        if (auto *sz = em->getModule<engine::SizeOverLifetimeModule>())
                        {
                            if (ImGui::TreeNodeEx("Size##SizeMod", ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                ImGui::DragFloat2("Base Size (W x H)", &sz->baseSize.x, 0.001f, 0.0001f, 1000.0f);
                                ImGui::TreePop();
                            }
                        }
                        else if (ImGui::SmallButton("+ Size"))
                            em->addModule<engine::SizeOverLifetimeModule>();

                        // ColorOverLifetimeModule
                        if (auto *col = em->getModule<engine::ColorOverLifetimeModule>())
                        {
                            if (ImGui::TreeNodeEx("Color Over Lifetime##ColMod", ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                for (size_t gi = 0; gi < col->gradient.size(); ++gi)
                                {
                                    auto &pt = col->gradient[gi];
                                    ImGui::PushID(static_cast<int>(gi));
                                    ImGui::SetNextItemWidth(60.0f);
                                    ImGui::DragFloat("##GradTime", &pt.time, 0.01f, 0.0f, 1.0f, "%.2f");
                                    ImGui::SameLine();
                                    ImGui::ColorEdit4("##GradColor", &pt.color.r, ImGuiColorEditFlags_NoInputs);
                                    ImGui::PopID();
                                }
                                ImGui::TreePop();
                            }
                        }
                        else if (ImGui::SmallButton("+ Color"))
                            em->addModule<engine::ColorOverLifetimeModule>();

                        // RendererModule
                        if (auto *rend = em->getModule<engine::RendererModule>())
                        {
                            if (ImGui::TreeNodeEx("Renderer##RendMod", ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                static const char *blendModes[] = {"Alpha Blend", "Additive", "Premultiplied"};
                                static const char *facingModes[] = {"Camera Facing", "Velocity Aligned", "World Up"};
                                int blendIdx = static_cast<int>(rend->blendMode);
                                int facingIdx = static_cast<int>(rend->facingMode);
                                if (ImGui::Combo("Blend Mode", &blendIdx, blendModes, 3))
                                    rend->blendMode = static_cast<engine::ParticleBlendMode>(blendIdx);
                                if (ImGui::Combo("Facing Mode", &facingIdx, facingModes, 3))
                                    rend->facingMode = static_cast<engine::ParticleFacingMode>(facingIdx);
                                ImGui::Checkbox("Soft Particles", &rend->softParticles);
                                if (rend->softParticles)
                                    ImGui::DragFloat("Soft Range", &rend->softParticleRange, 0.1f, 0.0f, 100.0f);

                                const auto currentProject = m_currentProject.lock();
                                const std::filesystem::path projectRoot = currentProject ? std::filesystem::path(currentProject->fullPath) : std::filesystem::path{};

                                ImGui::Separator();
                                ImGui::TextUnformatted("Particle Texture");

                                VkDescriptorSet previewSet = m_assetsPreviewSystem.getPlaceholder();
                                if (!rend->texturePath.empty())
                                    previewSet = m_assetsPreviewSystem.getOrRequestTexturePreview(rend->texturePath);

                                ImGui::Image(previewSet, ImVec2(52.0f, 52.0f));
                                ImGui::SameLine();
                                ImGui::BeginGroup();
                                ImGui::TextWrapped("%s", formatTexturePathForDisplay(rend->texturePath, projectRoot).c_str());
                                if (!rend->texturePath.empty() && ImGui::Button("Clear Texture"))
                                    rend->texturePath.clear();
                                ImGui::EndGroup();

                                ImGui::Button("Drop texture asset here##ParticleTextureDrop");
                                if (ImGui::BeginDragDropTarget())
                                {
                                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                                    {
                                        std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                                        if (isParticleTextureAssetPath(droppedPath))
                                        {
                                            rend->texturePath = normalizePathAgainstProjectRoot(droppedPath, projectRoot);
                                            m_notificationManager.showSuccess("Particle texture applied");
                                        }
                                        else
                                            m_notificationManager.showWarning("Drop a texture asset for particles");
                                    }
                                    ImGui::EndDragDropTarget();
                                }

                                if (ImGui::Button("Select Texture##ParticleTexturePicker"))
                                    ImGui::OpenPopup("ParticleTexturePickerPopup");

                                if (ImGui::BeginPopup("ParticleTexturePickerPopup"))
                                {
                                    static char textureFilter[128] = "";
                                    ImGui::InputText("Search##ParticleTextureFilter", textureFilter, sizeof(textureFilter));

                                    const std::vector<std::string> candidates = collectProjectParticleTextureCandidates(projectRoot);
                                    const std::string filterText = toLowerCopy(std::string(textureFilter));

                                    if (candidates.empty())
                                        ImGui::TextDisabled("No textures found in project");

                                    for (const std::string &candidatePath : candidates)
                                    {
                                        const std::string displayPath = formatTexturePathForDisplay(candidatePath, projectRoot);
                                        const std::string loweredDisplay = toLowerCopy(displayPath);
                                        if (!filterText.empty() && loweredDisplay.find(filterText) == std::string::npos)
                                            continue;

                                        const bool isSelected = candidatePath == rend->texturePath;
                                        if (ImGui::Selectable(displayPath.c_str(), isSelected))
                                        {
                                            rend->texturePath = candidatePath;
                                            m_notificationManager.showSuccess("Particle texture selected");
                                            ImGui::CloseCurrentPopup();
                                            break;
                                        }
                                    }

                                    ImGui::EndPopup();
                                }
                                ImGui::TreePop();
                            }
                        }
                        else if (ImGui::SmallButton("+ Renderer"))
                            em->addModule<engine::RendererModule>();

                        // VelocityOverLifetimeModule
                        if (auto *volm = em->getModule<engine::VelocityOverLifetimeModule>())
                        {
                            if (ImGui::TreeNodeEx("Velocity Over Lifetime##VOLMod", ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                ImGui::TextUnformatted("Speed Multiplier Curve:");
                                for (size_t ci = 0; ci < volm->speedCurve.size(); ++ci)
                                {
                                    ImGui::PushID(static_cast<int>(ci));
                                    ImGui::DragFloat("t", &volm->speedCurve[ci].time, 0.01f, 0.0f, 1.0f);
                                    ImGui::SameLine();
                                    ImGui::DragFloat("v", &volm->speedCurve[ci].value, 0.01f, 0.0f, 10.0f);
                                    ImGui::PopID();
                                }
                                if (ImGui::SmallButton("+ Point##VOL"))
                                    volm->speedCurve.push_back({1.0f, 1.0f});
                                ImGui::TreePop();
                            }
                        }
                        else if (ImGui::SmallButton("+ Velocity Over Lifetime"))
                            em->addModule<engine::VelocityOverLifetimeModule>();

                        // RotationOverLifetimeModule
                        if (auto *rolm = em->getModule<engine::RotationOverLifetimeModule>())
                        {
                            if (ImGui::TreeNodeEx("Rotation Over Lifetime##ROLMod", ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                ImGui::DragFloat("Min (rad/s)", &rolm->angularVelocityMin, 0.01f, -100.0f, 100.0f);
                                ImGui::DragFloat("Max (rad/s)", &rolm->angularVelocityMax, 0.01f, -100.0f, 100.0f);
                                ImGui::TreePop();
                            }
                        }
                        else if (ImGui::SmallButton("+ Rotation Over Lifetime"))
                            em->addModule<engine::RotationOverLifetimeModule>();

                        // TurbulenceModule
                        if (auto *turb = em->getModule<engine::TurbulenceModule>())
                        {
                            if (ImGui::TreeNodeEx("Turbulence##TurbMod", ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                ImGui::DragFloat("Strength", &turb->strength, 0.01f, 0.0f, 100.0f);
                                ImGui::DragFloat("Frequency", &turb->frequency, 0.01f, 0.01f, 100.0f);
                                ImGui::DragFloat("Scroll Speed", &turb->scrollSpeed, 0.01f, 0.0f, 100.0f);
                                ImGui::TreePop();
                            }
                        }
                        else if (ImGui::SmallButton("+ Turbulence"))
                            em->addModule<engine::TurbulenceModule>();

                        // Remove emitter
                        ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(160, 40, 40, 255));
                        if (ImGui::Button("Remove Emitter"))
                            emitterToRemove = em->name;
                        ImGui::PopStyleColor();
                    }

                    ImGui::PopID();

                    if (!emitterToRemove.empty())
                        break;
                }

                if (!emitterToRemove.empty())
                    ps->removeEmitter(emitterToRemove);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(160, 40, 40, 255));
            if (ImGui::Button("Remove Particle System"))
            {
                ImGui::PopStyleColor();
                ImGui::PopID();
                m_selectedEntity->removeComponent<engine::ParticleSystemComponent>();
                return;
            }
            ImGui::PopStyleColor();
        }

        ImGui::PopID();
    }
}

ELIX_NESTED_NAMESPACE_END
