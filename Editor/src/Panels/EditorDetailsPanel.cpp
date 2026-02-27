#include "Editor/Editor.hpp"

#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Components/CameraComponent.hpp"
#include "Engine/Components/CollisionComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/RigidBodyComponent.hpp"
#include "Engine/Components/ScriptComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Primitives.hpp"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/common.hpp>

#include <algorithm>
#include <cstring>

namespace
{
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

    bool createCollisionComponent(elix::engine::Scene *scene,
                                  elix::engine::Entity *entity,
                                  elix::engine::CollisionComponent::ShapeType shapeType)
    {
        if (!scene || !entity)
            return false;

        auto *transformComponent = entity->getComponent<elix::engine::Transform3DComponent>();
        if (!transformComponent)
            return false;

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
            {
                // PhysX capsule axis is +X by default; rotate it to +Y to match editor expectation.
                shape->setLocalPose(physx::PxTransform(physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0.0f, 0.0f, 1.0f))));
            }
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
} // namespace

ELIX_NESTED_NAMESPACE_BEGIN(editor)
void Editor::drawDetails()
{
    ImGui::Begin("Details");

    if (m_detailsContext == DetailsContext::Asset && !m_selectedAssetPath.empty())
    {
        drawAssetDetails();
        return ImGui::End();
    }

    if (!m_selectedEntity)
    {
        ImGui::Text("Select an object or asset to view details");
        return ImGui::End();
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

    const auto *parentEntity = m_selectedEntity->getParent();
    ImGui::Text("Parent: %s", parentEntity ? parentEntity->getName().c_str() : "<Root>");
    if (parentEntity && ImGui::Button("Detach From Parent"))
        m_selectedEntity->clearParent();

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

        ImGui::Separator();

        ImGui::Text("Common");

        if (ImGui::Button("Camera"))
        {
            m_selectedEntity->addComponent<engine::CameraComponent>();
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::Button("RigidBody"))
        {
            physx::PxTransform transform = makePxTransformFromEntity(m_selectedEntity);
            auto rigid = m_scene->getPhysicsScene().createDynamic(transform);
            auto rigidComponent = m_selectedEntity->addComponent<engine::RigidBodyComponent>(rigid);

            if (auto collisionComponent = m_selectedEntity->getComponent<engine::CollisionComponent>())
            {
                if (collisionComponent->getActor())
                {
                    m_scene->getPhysicsScene().removeActor(*collisionComponent->getActor(), true, true);
                    collisionComponent->removeActor();
                }

                rigidComponent->getRigidActor()->attachShape(*collisionComponent->getShape());

                if (auto *dynamicBody = rigidComponent->getRigidActor()->is<physx::PxRigidDynamic>())
                    physx::PxRigidBodyExt::updateMassAndInertia(*dynamicBody, 10.0f);
            }
        }

        if (ImGui::Button("Box Collision"))
        {
            if (createCollisionComponent(m_scene.get(), m_selectedEntity, engine::CollisionComponent::ShapeType::BOX))
                m_notificationManager.showSuccess("Box collision added");
            else
                m_notificationManager.showError("Failed to add box collision");
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::Button("Capsule Collision"))
        {
            if (createCollisionComponent(m_scene.get(), m_selectedEntity, engine::CollisionComponent::ShapeType::CAPSULE))
                m_notificationManager.showSuccess("Capsule collision added");
            else
                m_notificationManager.showError("Failed to add capsule collision");
            ImGui::CloseCurrentPopup();
        }

        ImGui::Button("Audio");

        if (ImGui::Button("Light"))
        {
            m_selectedEntity->addComponent<engine::LightComponent>(engine::LightComponent::LightType::POINT);
            ImGui::CloseCurrentPopup();
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
                if (ImGui::BeginTable("TransformTable", 2, ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Position");

                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushID("Position");
                    auto position = transformComponent->getPosition();

                    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(100, 100, 100, 255));
                    if (ImGui::Button("R"))
                        position = glm::vec3(0.0f);
                    ImGui::PopStyleColor();
                    ImGui::SameLine();

                    ImGui::DragFloat3("##Position", &position.x, 0.01f);

                    transformComponent->setPosition(position);

                    // X/Y/Z colored drag
                    // float* values[3] = { &pos.x, &pos.y, &pos.z };
                    // ImVec4 colors[3] = { ImVec4(0.8f,0.2f,0.2f,1.0f), ImVec4(0.2f,0.8f,0.2f,1.0f), ImVec4(0.2f,0.2f,0.8f,1.0f) };
                    // ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4,2));
                    // for (int i = 0; i < 3; i++)
                    // {
                    //     ImGui::PushStyleColor(ImGuiCol_Text, colors[i]);
                    //     ImGui::DragFloat(i==0 ? "##X" : (i==1?"##Y":"##Z"), values[i], 0.1f);
                    //     ImGui::PopStyleColor();
                    //     if(i<2) ImGui::SameLine();
                    // }
                    // ImGui::PopStyleVar();
                    // transformComponent->setPosition(pos);
                    ImGui::PopID();

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Rotation");
                    ImGui::TableSetColumnIndex(1);
                    auto euler = transformComponent->getEulerDegrees();
                    if (ImGui::DragFloat3("##Rotation", &euler.x, 0.1f))
                        transformComponent->setEulerDegrees(euler);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Scale");
                    ImGui::TableSetColumnIndex(1);
                    auto scale = transformComponent->getScale();
                    if (ImGui::DragFloat3("##Scale", &scale.x, 0.01f, 0.0f, 100.0f))
                        transformComponent->setScale(scale);

                    ImGui::EndTable();
                }
            }
        }
        else if (auto lightComponent = dynamic_cast<engine::LightComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
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
                            if (applyMaterialToSelectedEntity(droppedPath, std::nullopt, true))
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
                        openMaterialEditor(overridePath);

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                        {
                            std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                            const std::string extension = std::filesystem::path(droppedPath).extension().string();

                            if (extension == ".elixmat")
                            {
                                if (applyMaterialToSelectedEntity(droppedPath, meshIndex))
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
                        openMaterialEditor(overridePath);

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
                            if (applyMaterialToSelectedEntity(droppedPath, std::nullopt, true))
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
                        openMaterialEditor(overridePath);

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                        {
                            std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                            const std::string extension = std::filesystem::path(droppedPath).extension().string();

                            if (extension == ".elixmat")
                            {
                                if (applyMaterialToSelectedEntity(droppedPath, meshIndex))
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
                        openMaterialEditor(overridePath);

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

                if (animations.empty())
                {
                    ImGui::TextDisabled("No animation clips imported for this model");
                }
                else
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

                            if (ImGui::Selectable(animationName.c_str(), isSelected))
                            {
                                selectedAnimationIndex = animationIndex;
                                animatorComponent->setSelectedAnimationIndex(selectedAnimationIndex);
                            }

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
                }
            }
        }
        else if (auto cameraComponent = dynamic_cast<engine::CameraComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto camera = cameraComponent->getCamera();
                float yaw = camera->getYaw();
                float pitch = camera->getPitch();
                glm::vec3 position = camera->getPosition();
                float fov = camera->getFOV();

                if (ImGui::DragFloat("Yaw", &yaw))
                    camera->setYaw(yaw);

                if (ImGui::DragFloat("Pitch", &pitch))
                    camera->setPitch(pitch);

                if (ImGui::DragFloat("FOV", &fov))
                    camera->setFOV(fov);

                if (ImGui::DragFloat3("Camera position", &position.x, 0.1f, 0.0f))
                    camera->setPosition(position);
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
        else if (auto collisionComponent = dynamic_cast<engine::CollisionComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Collision", ImGuiTreeNodeFlags_DefaultOpen))
            {
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
                    ImGui::End();
                    return;
                }
            }
        }
    }

    const auto scriptComponents = m_selectedEntity->getComponents<engine::ScriptComponent>();

    if (!scriptComponents.empty() && ImGui::CollapsingHeader("Scripts", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (size_t scriptIndex = 0; scriptIndex < scriptComponents.size(); ++scriptIndex)
        {
            const auto *scriptComponent = scriptComponents[scriptIndex];
            const std::string &scriptName = scriptComponent->getScriptName();
            const std::string displayName = scriptName.empty() ? std::string("<Unnamed Script>") : scriptName;

            ImGui::PushID(static_cast<int>(scriptIndex));
            ImGui::BulletText("%s", displayName.c_str());
            ImGui::PopID();
        }
    }

    ImGui::End();
}

ELIX_NESTED_NAMESPACE_END
