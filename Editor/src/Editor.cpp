#include "Editor/Editor.hpp"

#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <cstring>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

void ShowMainDockSpace()
{
    static bool dockspaceOpen = true;
    constexpr static bool showDockingFullscreen = false;
    constexpr static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    if (showDockingFullscreen)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    }

    ImGui::Begin("DockSpace Demo", &dockspaceOpen, windowFlags);

    if (showDockingFullscreen)
        ImGui::PopStyleVar(2);

    ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);

    ImGui::End();

    static bool first_time = true;

    if (first_time) 
    {
        first_time = false;

        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

        ImGuiID dockMainId = dockspaceId;
        ImGuiID dockIdLeft;
        ImGuiID dockIdRight;
        ImGuiID dockIdDown;

        dockIdLeft = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, 0.2f, nullptr, &dockMainId);
        dockIdRight = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.25f, nullptr, &dockMainId);
        dockIdDown = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.3f, nullptr, &dockMainId);

        ImGuiID dockIdLeftTop;
        ImGuiID dockIdLeftBottom;
        dockIdLeftTop = dockIdLeft;
        dockIdLeftBottom = ImGui::DockBuilderSplitNode(dockIdLeftTop, ImGuiDir_Down, 0.7f, nullptr, &dockIdLeftTop);

        ImGui::DockBuilderDockWindow("Hierarchy", dockIdLeftTop);
        ImGui::DockBuilderDockWindow("Benchmark", dockIdLeftBottom);
        ImGui::DockBuilderDockWindow("Details", dockIdRight);
        ImGui::DockBuilderDockWindow("Content Browser", dockIdDown);
        ImGui::DockBuilderDockWindow("Viewport", dockMainId);

        ImGui::DockBuilderFinish(dockspaceId);
    }
}

void Editor::drawFrame(VkDescriptorSet viewportDescriptorSet)
{
    ShowMainDockSpace();

    if(viewportDescriptorSet)
        drawViewport(viewportDescriptorSet);
    
    drawHierarchy();
    drawBenchmark();
    drawDetails();
}

void Editor::drawDetails()
{
    ImGui::Begin("Details");

    if(!m_selectedEntity)
        return ImGui::End();
        
    char buffer[128];
    std::strncpy(buffer, m_selectedEntity->getName().c_str(), sizeof(buffer));
    if (ImGui::InputText("Name", buffer, sizeof(buffer)))
        m_selectedEntity->setName(std::string(buffer));

    ImGui::Separator();

    for(const auto& [_, component] : m_selectedEntity->getSingleComponents())
    {
        if(auto transformComponent = dynamic_cast<engine::Transform3DComponent*>(component.get()))
        {
            ImGui::Separator();
            ImGui::Text("Transform");

            ImGui::DragFloat3("Position", &transformComponent->getPosition().x, 0.1f);

            glm::vec3 euler = transformComponent->getEulerDegrees();
            if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f))
                transformComponent->setEulerDegrees(euler);

            ImGui::DragFloat3("Scale", &transformComponent->getScale().x, 0.1f);

            ImGui::Separator();
        }
        else if(auto lightComponent = dynamic_cast<engine::LightComponent*>(component.get()))
        {
            ImGui::Separator();
            ImGui::Text("Light");

            auto lightType = lightComponent->getLightType();
            auto light = lightComponent->getLight();

            static const std::vector<const char*> lightTypes
            {
                "Directional",
                "Spot",
                "Point"
            };

            static int currentLighType = 0;

            switch(lightType)
            {
                case engine::LightComponent::LightType::DIRECTIONAL: currentLighType = 0; break;
                case engine::LightComponent::LightType::SPOT: currentLighType = 1; break;
                case engine::LightComponent::LightType::POINT: currentLighType = 2; break;
            };

            if(ImGui::Combo("Light type", &currentLighType, lightTypes.data(), lightTypes.size()))
            {
                if(currentLighType == 0)
                    lightComponent->changeLightType(engine::LightComponent::LightType::DIRECTIONAL);
                else if(currentLighType == 1)
                    lightComponent->changeLightType(engine::LightComponent::LightType::SPOT);
                else if(currentLighType == 2)
                    lightComponent->changeLightType(engine::LightComponent::LightType::POINT);
                
                lightType = lightComponent->getLightType();
                light = lightComponent->getLight();
            }

            ImGui::DragFloat3("Light position", &light->position.x, 0.1f, 0.0f);
            ImGui::ColorEdit3("Light color", &light->color.x);
            ImGui::DragFloat("Light strength", &light->strength, 0.1f, 0.0f);

            if(lightType == engine::LightComponent::LightType::POINT)
            {
                auto pointLight = dynamic_cast<engine::PointLight*>(light.get());
                ImGui::DragFloat("Light radius", &pointLight->radius, 0.1, 0.0f, 360.0f);
            }
            else if(lightType == engine::LightComponent::LightType::DIRECTIONAL)
            {
                auto directionalLight = dynamic_cast<engine::DirectionalLight*>(light.get());
                ImGui::DragFloat3("Light direction", &directionalLight->direction.x);
            }
            else if(lightType == engine::LightComponent::LightType::SPOT)
            {
                auto spotLight = dynamic_cast<engine::SpotLight*>(light.get());
                ImGui::DragFloat3("Light direction", &spotLight->direction.x);
                ImGui::DragFloat("Inner", &spotLight->innerAngle);
                ImGui::DragFloat("Outer", &spotLight->outerAngle);
            }


            ImGui::Separator();
        }
        else if(auto staticComponent = dynamic_cast<engine::StaticMeshComponent*>(component.get()))
        {
            ImGui::Separator();
            ImGui::Text("Static mesh");

            ImGui::Separator();
        }
    }

    ImGui::End();
}

void Editor::drawViewport(VkDescriptorSet viewportDescriptorSet)
{
    ImGui::Begin("Viewport");
    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    ImGui::Image(viewportDescriptorSet, ImVec2(viewportPanelSize.x, viewportPanelSize.y));
    ImGui::End();
}

void Editor::drawBenchmark()
{
    ImGui::Begin("Benchmark");
    float fps = ImGui::GetIO().Framerate;
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Frame time: %.3f ms", 1000.0f / fps);
    
    if(ImGui::Button("Save scene"))
        if(m_scene)
            m_scene->saveSceneToFile("./resources/scenes/default_scene.scene");
    
    ImGui::End();
}

void Editor::drawHierarchy()
{
    ImGui::Begin("Hierarchy");

    if(!m_scene)
        return ImGui::End();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));

    for(auto& entity : m_scene->getEntities())
    {
        // ImGui::PushID(entity->getID());

        auto entityName = entity->getName().c_str();
        
        bool selected = (entity == m_selectedEntity);

        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;

        if (selected)
            nodeFlags |= ImGuiTreeNodeFlags_Selected;

        bool nodeOpen = ImGui::TreeNodeEx(entityName, nodeFlags);

        if (ImGui::IsItemClicked())
            m_selectedEntity = entity;

        if(nodeOpen)
            ImGui::TreePop();

        // ImGui::PopID();
    }

    ImGui::PopStyleVar(2);

    ImGui::End();
}

ELIX_NESTED_NAMESPACE_END