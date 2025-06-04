#include "UILight.hpp"

#include <imgui.h>
#include <string>
#include <vector>

void UILight::draw(lighting::Light *light)
{
    if (!light)
        return;

    glm::vec3 color = light->color;

    if (ImGui::ColorEdit3(("Base Color##" + std::to_string(1)).c_str(), &color.x))
        light->color = color;

    glm::vec3 lightPosition = light->position;
    if (ImGui::DragFloat3("Light position", &lightPosition[0], 0.1f))
        light->position = lightPosition;

    float strength = light->strength;;
    if (ImGui::DragFloat("Strength", &strength, 0.1f))
        light->strength = strength;

    float radius = light->radius;
    if (ImGui::DragFloat("Radius", &radius, 0.1f))
        light->radius = radius;

    static int selectedLightType = 0;

    selectedLightType = static_cast<int>(light->type);

    std::vector<const char*> lightTypes{"DIRECTIONAL_LIGHT", "POINT_LIGHT", "SPOT_LIGHT"};

    if (ImGui::Combo("##Light type combo", &selectedLightType, lightTypes.data(), static_cast<int>(lightTypes.size())))
    {
        light->type = static_cast<lighting::LightType>(selectedLightType);
    }

    glm::vec3 direction = light->direction;

    if (ImGui::DragFloat3("Direction", &direction[0], 0.1f))
        light->direction = direction;

    float cutoff = light->cutoff;

    if (ImGui::DragFloat("Cutoff", &cutoff, 0.1f))
        light->cutoff = cutoff;

    float outerCutoff = light->outerCutoff;

    if (ImGui::DragFloat("Outer Cutoff", &outerCutoff, 0.1f))
        light->outerCutoff = outerCutoff;
}
