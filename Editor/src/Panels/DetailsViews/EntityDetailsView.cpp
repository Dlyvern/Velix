#include "Editor/Panels/DetailsViews/EntityDetailsView.hpp"

#include "Editor/Editor.hpp"

#include "Engine/Assets/AssetsLoader.hpp"

#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Components/AudioComponent.hpp"
#include "Engine/Components/ReflectionProbeComponent.hpp"
#include "Engine/Components/DecalComponent.hpp"
#include "Engine/Components/CameraComponent.hpp"
#include "Core/VulkanContext.hpp"
#include "Engine/Components/CharacterMovementComponent.hpp"
#include "Engine/Components/CollisionComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/ParticleSystemComponent.hpp"
#include "Engine/Components/RigidBodyComponent.hpp"
#include "Engine/Components/ScriptComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Particles/Modules/ColorOverLifetimeModule.hpp"
#include "Engine/Particles/Modules/ForceModule.hpp"
#include "Engine/Particles/Modules/InitialVelocityModule.hpp"
#include "Engine/Particles/Modules/LifetimeModule.hpp"
#include "Engine/Particles/Modules/RendererModule.hpp"
#include "Engine/Particles/Modules/SizeOverLifetimeModule.hpp"
#include "Engine/Particles/Modules/SpawnModule.hpp"
#include "Engine/Primitives.hpp"

#include <imgui.h>
#include <glm/common.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
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
        if (projectRoot.empty() || !std::filesystem::exists(projectRoot))
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
}

ELIX_NESTED_NAMESPACE_BEGIN(editor)

bool EntityDetailsView::canDraw(const Editor &editor) const
{
    return editor.m_detailsContext != Editor::DetailsContext::Asset &&
           editor.m_selectedEntity != nullptr;
}

void EntityDetailsView::draw(Editor &editor)
{
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

        if (ImGui::Button("Character Movement"))
        {
            if (m_selectedEntity->getComponent<engine::CharacterMovementComponent>())
            {
                m_notificationManager.showWarning("Character movement already exists");
            }
            else if (m_selectedEntity->getComponent<engine::CollisionComponent>())
            {
                m_notificationManager.showWarning("Remove Collision component before adding Character Movement");
            }
            else
            {
                auto *transform = m_selectedEntity->getComponent<engine::Transform3DComponent>();
                const glm::vec3 safeScale = transform
                                                ? glm::max(glm::abs(transform->getScale()), glm::vec3(0.1f))
                                                : glm::vec3(1.0f);
                const float radius = std::max(0.1f, std::max(safeScale.x, safeScale.z) * 0.25f);
                const float height = std::max(0.5f, safeScale.y);
                m_selectedEntity->addComponent<engine::CharacterMovementComponent>(m_scene.get(), radius, height);
                m_notificationManager.showSuccess("Character movement added");
            }

            ImGui::CloseCurrentPopup();
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

        if (ImGui::Button("Audio Source"))
        {
            m_selectedEntity->addComponent<engine::AudioComponent>();
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::Button("Animator"))
        {
            if (!m_selectedEntity->getComponent<engine::AnimatorComponent>())
            {
                m_selectedEntity->addComponent<engine::AnimatorComponent>();
                ImGui::CloseCurrentPopup();
            }
            else
            {
                m_notificationManager.showWarning("Animator already exists on this entity");
            }
        }

        if (ImGui::Button("Light"))
        {
            m_selectedEntity->addComponent<engine::LightComponent>(engine::LightComponent::LightType::POINT);
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::Button("Reflection Probe"))
        {
            if (!m_selectedEntity->getComponent<engine::ReflectionProbeComponent>())
            {
                m_selectedEntity->addComponent<engine::ReflectionProbeComponent>();
                ImGui::CloseCurrentPopup();
            }
            else
            {
                m_notificationManager.showWarning("Reflection Probe already exists on this entity");
            }
        }

        if (ImGui::Button("Particle System"))
        {
            auto *psComp = m_selectedEntity->addComponent<engine::ParticleSystemComponent>();
            auto newSys = std::make_shared<engine::ParticleSystem>();
            newSys->name = "Particle System";
            auto *emitter = newSys->addEmitter("Emitter 0");
            emitter->addModule<engine::SpawnModule>();
            emitter->addModule<engine::LifetimeModule>();
            emitter->addModule<engine::InitialVelocityModule>();
            emitter->addModule<engine::SizeOverLifetimeModule>();
            emitter->addModule<engine::ForceModule>();
            emitter->addModule<engine::RendererModule>();
            emitter->addModule<engine::ColorOverLifetimeModule>();
            psComp->setParticleSystem(newSys);
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::Button("Particle System (Rain)"))
        {
            auto *psComp = m_selectedEntity->addComponent<engine::ParticleSystemComponent>();
            psComp->setParticleSystem(engine::ParticleSystem::createRain());
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::Button("Decal"))
        {
            if (!m_selectedEntity->getComponent<engine::DecalComponent>())
            {
                m_selectedEntity->addComponent<engine::DecalComponent>();
                ImGui::CloseCurrentPopup();
            }
            else
            {
                m_notificationManager.showWarning("Decal already exists on this entity");
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
                        animatorComponent->clearTree();

                    // Runtime state info
                    const std::string curState = animatorComponent->getCurrentStateName();
                    ImGui::Text("State: %s", curState.empty() ? "(none)" : curState.c_str());
                    if (animatorComponent->isInTransition())
                        ImGui::TextDisabled("  (transitioning %.0f%%)", animatorComponent->getCurrentStateNormalizedTime() * 100.0f);

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
        else if (auto cameraComponent = dynamic_cast<engine::CameraComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
            {
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
                float radius = characterMovement->getCapsuleRadius();
                float height = characterMovement->getCapsuleHeight();
                bool capsuleChanged = false;
                capsuleChanged |= ImGui::DragFloat("Capsule Radius", &radius, 0.01f, 0.05f, 1000.0f, "%.3f");
                capsuleChanged |= ImGui::DragFloat("Capsule Height", &height, 0.01f, 0.1f, 1000.0f, "%.3f");
                if (capsuleChanged)
                    characterMovement->setCapsule(radius, height);

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
                        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
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

                ImGui::DragFloat("Radius##ProbeRadius",    &probeComponent->radius,    0.1f, 0.1f, 500.0f, "%.1f");
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
        else if (auto *decalComponent = dynamic_cast<engine::DecalComponent *>(component.get()))
        {
            if (ImGui::CollapsingHeader("Decal", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushID("DecalComp");

                const bool hasMaterial = decalComponent->material != nullptr;
                const std::string matLabel = hasMaterial
                    ? (decalComponent->material->getName().empty()
                        ? "<Unnamed>"
                        : decalComponent->material->getName())
                    : "<None>";

                ImGui::TextUnformatted("Material:");
                ImGui::SameLine();
                ImGui::TextDisabled("%s", matLabel.c_str());

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
                                m_notificationManager.showSuccess("Decal material assigned");
                            }
                            else
                            {
                                m_notificationManager.showError("Failed to load decal material");
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (hasMaterial && ImGui::Button("Clear Material##DecalClear"))
                    decalComponent->material = nullptr;

                ImGui::Separator();

                ImGui::DragFloat3("Size##DecalSize",       &decalComponent->size.x,   0.05f, 0.01f, 500.0f, "%.2f");
                ImGui::SliderFloat("Opacity##DecalOpacity", &decalComponent->opacity,  0.0f,  1.0f,  "%.2f");
                ImGui::DragInt("Sort Order##DecalSort",    &decalComponent->sortOrder, 1.0f, -100, 100);

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
                auto *scriptInstance = scriptComponent->getScript();
                if (!scriptInstance)
                {
                    ImGui::TextDisabled("Script instance is null");
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

            if (!ps)
            {
                if (ImGui::Button("Create Empty System"))
                {
                    auto newSys = std::make_shared<engine::ParticleSystem>();
                    newSys->name = "Particle System";
                    psComp->setParticleSystem(newSys);
                }
                ImGui::SameLine();
                if (ImGui::Button("Create Rain Preset"))
                    psComp->setParticleSystem(engine::ParticleSystem::createRain());
            }
            else
            {
                char sysNameBuf[128];
                std::strncpy(sysNameBuf, ps->name.c_str(), sizeof(sysNameBuf));
                if (ImGui::InputText("Name##SysName", sysNameBuf, sizeof(sysNameBuf)))
                    ps->name = sysNameBuf;

                if (ImGui::Button("Apply Rain Preset"))
                {
                    psComp->setParticleSystem(engine::ParticleSystem::createRain());
                    ps = psComp->getParticleSystem();
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
