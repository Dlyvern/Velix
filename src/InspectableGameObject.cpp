#include "InspectableGameObject.hpp"

#include <imgui.h>

#include <ElixirCore/ScriptsRegister.hpp>
#include <ElixirCore/AnimatorComponent.hpp>
#include <ElixirCore/GameObject.hpp>
#include <ElixirCore/ParticleComponent.hpp>
#include <ElixirCore/MeshComponent.hpp>
#include <ElixirCore/LightComponent.hpp>
#include <ElixirCore/CameraComponent.hpp>
#include <ElixirCore/ScriptComponent.hpp>
#include <ElixirCore/AudioComponent.hpp>
#include <ElixirCore/ReflectedObject.hpp>
#include <ElixirCore/Logger.hpp>

#include "ProjectManager.hpp"
#include "Engine.hpp"
#include "UITransform.hpp"
#include "UIInputText.hpp"
#include "UIMesh.hpp"
#include "UILight.hpp"


#include <glm/gtc/type_ptr.hpp>
#include <cstdlib>
#include <glm/gtc/random.hpp>

#include <ElixirCore/LibrariesLoader.hpp>

class GravityModule : public elix::ParticleModule
{
public:
    GravityModule(glm::vec3 gravity) : m_gravity(gravity) {}

    void update(elix::Particle &particle, float deltaTime) override
    {
        particle.velocity += m_gravity * deltaTime;
        particle.position += particle.velocity * deltaTime;
    }

    void onSpawn(elix::Particle &particle) override {}
    void onDeath(elix::Particle &particle) override {}

private:
    glm::vec3 m_gravity;
};


class RandomSpawnBoxModule : public elix::ParticleModule
{
public:
    RandomSpawnBoxModule(glm::vec3 min, glm::vec3 max) : m_min(min), m_max(max) {}

    void onSpawn(elix::Particle &particle) override
    {
        particle.position = glm::linearRand(m_min, m_max);
    }

    void update(elix::Particle &, float) override {}
    void onDeath(elix::Particle &) override {}

private:
    glm::vec3 m_min;
    glm::vec3 m_max;
};

void displayBonesHierarchy(Skeleton *skeleton, common::BoneInfo *parent = nullptr)
{
    if (!skeleton)
        return;

    if (const auto bone = parent ? parent : skeleton->getParent(); ImGui::TreeNode(bone->name.c_str()))
    {
        for (const int &childBone : bone->children)
        {
            displayBonesHierarchy(skeleton, skeleton->getBone(childBone));
        }

        ImGui::TreePop();
    }
}

InspectableGameObject::InspectableGameObject(GameObject* gameObject) : m_gameObject(gameObject) {}

void InspectableGameObject::draw()
{
    if(!m_gameObject)
        return;

    std::string objectName = m_gameObject->getName();

    if (UIInputText::draw(objectName))
        m_gameObject->setName(objectName);

    if (ImGui::BeginTabBar("Tabs"))
    {
        if (ImGui::BeginTabItem("Transform"))
        {
            UITransform::draw(m_gameObject);

            if (m_gameObject->hasComponent<MeshComponent>())
                if (auto model = m_gameObject->getComponent<MeshComponent>()->getModel())
                    for (int meshIndex = 0; meshIndex < model->getNumMeshes(); meshIndex++)
                        UIMesh::draw(model->getMesh(meshIndex), meshIndex, m_gameObject);

            ImGui::SeparatorText("Components");

            if(auto audioComponents = m_gameObject->getComponents<elix::AudioComponent>(); !audioComponents.empty())
            {
                int index{0};

                for(const auto& audioComponent : audioComponents)
                {
                    const std::string header = "Audio" + std::to_string(index);

                    if(ImGui::CollapsingHeader(header.c_str()))
                    {
                        ImGui::Text("Sound: %s", audioComponent->getSoundPath().c_str());
                        
                        float volume = audioComponent->getVolume();

                        if(ImGui::DragFloat("Volume", &volume))
                            audioComponent->setVolume(volume);
                        
                        bool isLooping = audioComponent->isLooping();

                        if(ImGui::Checkbox("Is looping", &isLooping))
                            audioComponent->setLooping(isLooping);

                        if (ImGui::BeginDragDropTarget())
                        {
                            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                            {
                                const auto *const info = static_cast<Editor::DraggingInfo*>(payload->Data);

                                if (info)
                                {
                                    std::filesystem::path path(info->name);
                                    audioComponent->setAudio(path.string());
                                    audioComponent->play();
                                }
                                else
                                    ELIX_LOG_WARN("Dragging info is not accessed");
                            }

                            ImGui::EndDragDropTarget();
                        }
                    }
                }
            }
            
            if (m_gameObject->hasComponent<AnimatorComponent>())
            {
                ImGui::CollapsingHeader("Animator");
            }

            if (m_gameObject->hasComponent<LightComponent>())
            {
                if (ImGui::CollapsingHeader("Light"))
                {
                    UILight::draw(m_gameObject->getComponent<LightComponent>());
                }
            }

            if (m_gameObject->hasComponent<ParticleComponent>())
            {
                ImGui::CollapsingHeader("Particle");
            }

            if (m_gameObject->hasComponent<elix::CameraComponent>())
            {
                if (ImGui::CollapsingHeader("Camera"))
                {
                    auto camera = m_gameObject->getComponent<elix::CameraComponent>();

                    auto position = camera->getPosition();
                    if (ImGui::DragFloat3("Camera position", &position[0], 0.1f))
                        camera->setPosition(position);
                }
            }

            if (m_gameObject->hasComponent<ScriptComponent>())
            {
                auto scriptComponent = m_gameObject->getComponent<ScriptComponent>();

                const auto &scripts = scriptComponent->getScripts();

                if (ImGui::CollapsingHeader("Scripts"))
                {
                    for (const auto &[scriptName, script] : scripts)
                    {
                        ImGui::Text("%s", scriptName.c_str());

                        ImGui::SameLine();

                        if (ImGui::Button("Start/Stop simulate"))
                            scriptComponent->setUpdateScripts(!scriptComponent->getUpdateScripts());
                            
                        ImGui::SeparatorText("Variables:");

                        if(auto reflectedScript = dynamic_cast<elix::ReflectedObject*>(script.get()))
                        {
                            for(const auto& [name, value] : reflectedScript->getProperties())
                            {
                                std::visit([&](auto&& arg)
                                {
                                    using T = std::decay_t<decltype(arg)>;

                                    if constexpr (std::is_same_v<T, bool>)
                                    {
                                        bool var = arg;

                                        if(ImGui::Checkbox(name.c_str(), &var))
                                            reflectedScript->setProperty(name, var);
                                    }
                                    else if constexpr (std::is_integral_v<T>)
                                    {
                                        int var = arg;

                                        if(ImGui::DragInt(name.c_str(), &var))
                                            reflectedScript->setProperty(name, var);
                                    }
                                    else if constexpr (std::is_same_v<T, float>)
                                    {
                                        
                                    }
                                }, value);
                            }
                        }

                    }

                    if(ImGui::Button("Attach script"))
                    {
                        ImGui::OpenPopup("AttachScriptPopup"); 
                    }

                    if(ImGui::BeginPopup("AttachScriptPopup"))
                    {
                        auto library = ProjectManager::instance().getCurrentProject()->getProjectLibrary();

                        if(library)
                        {
                            using GetScriptsRegisterFunc = ScriptsRegister *(*)();

                            auto getFunction = (GetScriptsRegisterFunc)elix::LibrariesLoader::getFunction("getScriptsRegister", library);

                            if (!getFunction)
                            {
                                ELIX_LOG_ERROR("Could not get function 'getScriptsRegister'");
                                return;
                            }

                            ScriptsRegister *s = getFunction();

                            using InitFunc = const char **(*)(int *);

                            InitFunc function = (InitFunc)elix::LibrariesLoader::getFunction("initScripts", library);

                            if (!function)
                            {
                                ELIX_LOG_ERROR("Could not get function 'initScripts'");
                                return;
                            }

                            int count = 0;

                            const char **scripts = function(&count);

                            for (int i = 0; i < count; ++i)
                            {
                                std::string scriptName = scripts[i];

                                auto script = s->createScript(scriptName);

                                if (!script)
                                    ELIX_LOG_ERROR("Could not find script");
                                else
                                {
                                    if(ImGui::MenuItem(scriptName.c_str()))
                                    {
                                        scriptComponent->addScript(script);
                                    }
                                }
                            }
                        }
                        ImGui::EndPopup();
                    }
                }
            }

            if (ImGui::Button("Add component"))
                ImGui::OpenPopup("AddComponentPopup");

            static char searchBuffer[128] = "";

            if (ImGui::BeginPopup("AddComponentPopup"))
            {
                ImGui::InputTextWithHint("##search", "Search...", searchBuffer, IM_ARRAYSIZE(searchBuffer));

                ImGui::Separator();

                const std::vector<std::string> availableComponents = {
                    "Animator",
                    "Script",
                    "Light",
                    "Camera",
                    "Particle",
                    "Audio"
                };

                for (const auto &comp : availableComponents)
                {
                    if (strlen(searchBuffer) == 0 || comp.find(searchBuffer) != std::string::npos)
                    {
                        if (ImGui::MenuItem(comp.c_str()))
                        {
                            if (comp == "Animator" && !m_gameObject->hasComponent<AnimatorComponent>())
                                m_gameObject->addComponent<AnimatorComponent>();
                            else if (comp == "Script" && !m_gameObject->hasComponent<ScriptComponent>())
                                m_gameObject->addComponent<ScriptComponent>();
                            else if (comp == "Light" && !m_gameObject->hasComponent<LightComponent>())
                            {
                                auto light = std::make_shared<lighting::Light>();
                                m_gameObject->addComponent<LightComponent>(light);
                                Engine::s_application->getScene()->addLight(light);
                            }
                            else if (comp == "Camera" && !m_gameObject->hasComponent<elix::CameraComponent>())
                            {
                                auto cameraComponent = m_gameObject->addComponent<elix::CameraComponent>();
                                //Maybe we can just re-use Application's camera                                    
                                //TODO add new camera to the scene
                            }
                            else if(comp == "Audio" && !m_gameObject->hasComponent<elix::AudioComponent>())
                            {
                                auto audioComponent = m_gameObject->addComponent<elix::AudioComponent>();
                            }
                            else if (comp == "Particle" && !m_gameObject->hasComponent<ParticleComponent>())
                            {
                                auto emitter = std::make_unique<elix::ParticleEmitter>();
                                emitter->setSpawnRate(1000.0f);
                                emitter->setLifetime(3.0f);
                                emitter->setInitialVelocity(glm::vec3(0.0f, -10.0f, 0.0f));

                                auto gravity = std::make_shared<GravityModule>(glm::vec3(0.0f, -9.81f, 0.0f));
                                emitter->addModule(gravity);

                                emitter->addModule(std::make_shared<class RandomSpawnBoxModule>(
                                    glm::vec3(-5.0f, 10.0f, -5.0f), glm::vec3(5.0f, 10.0f, 5.0f)));

                                auto system = std::make_unique<elix::ParticleSystem>();
                                system->addEmitter(std::move(emitter));

                                m_gameObject->addComponent<ParticleComponent>(std::move(system));
                            }

                            ImGui::CloseCurrentPopup();
                            break;
                        }
                    }
                }

                ImGui::EndPopup();
            }

            ImGui::EndTabItem();
        }

        if (m_gameObject->hasComponent<MeshComponent>() && ImGui::BeginTabItem("Bones"))
        {
            displayBonesHierarchy(m_gameObject->getComponent<MeshComponent>()->getModel()->getSkeleton());
            ImGui::EndTabItem();
        }

        if (m_gameObject->hasComponent<MeshComponent>() && ImGui::BeginTabItem("Animation"))
        {
            auto component = m_gameObject->getComponent<MeshComponent>();

            for (const auto &anim : component->getModel()->getAnimations())
            {
                if (ImGui::Button(anim->name.c_str()))
                    if (auto *animation = component->getModel()->getAnimation(anim->name))
                    {
                        if (!m_gameObject->hasComponent<AnimatorComponent>())
                            m_gameObject->addComponent<AnimatorComponent>();

                        animation->skeletonForAnimation = component->getModel()->getSkeleton();
                        m_gameObject->getComponent<AnimatorComponent>()->playAnimation(animation);
                    }
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}
