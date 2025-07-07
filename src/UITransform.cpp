#include "UITransform.hpp"

#include <imgui.h>

#include "VelixFlow/GameObject.hpp"

void UITransform::draw(GameObject *gameObject)
{
    glm::vec3 position = gameObject->getPosition();
    if (ImGui::DragFloat3("Position", &position[0], 0.1f))
        gameObject->setPosition(position);

    glm::vec3 rotation = gameObject->getRotation();
    if (ImGui::DragFloat3("Rotation", &rotation[0], 1.0f))
        gameObject->setRotation(rotation);

    glm::vec3 scale = gameObject->getScale();
    if (ImGui::DragFloat3("Scale", &scale[0], 0.1f))
        gameObject->setScale(scale);

}
