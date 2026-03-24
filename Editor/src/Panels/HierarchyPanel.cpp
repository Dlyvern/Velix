#include "Editor/Panels/HierarchyPanel.hpp"

#include "Core/Logger.hpp"
#include "Engine/Components/Transform3DComponent.hpp"

#include "imgui.h"
#include "glm/gtc/type_ptr.hpp"
#include "ImGuizmo.h"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

void HierarchyPanel::setSetSelectedEntityCallback(const std::function<void(engine::Entity *)> &function)
{
    m_setSelectedEntityCallback = function;
}

void HierarchyPanel::setSelectedEntity(engine::Entity *entity)
{
    m_selectedEntity = entity;
}

void HierarchyPanel::setAddEmptyEntityCallback(const std::function<void(const std::string &)> &function)
{
    m_addEmptyEntityCallback = function;
}

void HierarchyPanel::setAddPrimitiveEntityCallback(const std::function<void(const std::string &)> &function)
{
    m_addPrimitiveEntityCallback = function;
}

void HierarchyPanel::setScene(engine::Scene *scene)
{
    m_scene = scene;
}

void HierarchyPanel::drawContents()
{
    if (!m_scene)
        return;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));

    for (auto &entity : m_scene->getEntities())
        if (entity && !entity->getParent())
            drawHierarchyEntityNode(entity.get());

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY_ID"))
        {
            const uint32_t draggedId = *static_cast<const uint32_t *>(payload->Data);

            if (auto *draggedEntity = m_scene->getEntityById(draggedId))
            {
                if (draggedEntity->getParent())
                {
                    glm::mat4 worldMatrix(1.0f);
                    if (auto *transform = draggedEntity->getComponent<engine::Transform3DComponent>())
                        worldMatrix = transform->getMatrix();

                    draggedEntity->clearParent();

                    if (auto *transform = draggedEntity->getComponent<engine::Transform3DComponent>())
                    {
                        glm::vec3 translation, rotation, scale;
                        ImGuizmo::DecomposeMatrixToComponents(
                            glm::value_ptr(worldMatrix),
                            glm::value_ptr(translation),
                            glm::value_ptr(rotation),
                            glm::value_ptr(scale));

                        transform->setPosition(translation);
                        transform->setEulerDegrees(rotation);
                        transform->setScale(scale);
                    }
                }
            }
        }

        ImGui::EndDragDropTarget();
    }

    ImGui::PopStyleVar(2);

    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("HierarchyPopup");
    }

    if (ImGui::BeginPopup("HierarchyPopup"))
    {
        if (ImGui::Button("Add entity"))
        {
            ImGui::OpenPopup("EntityAddingPopup");
        }

        if (ImGui::BeginPopup("EntityAddingPopup"))
        {
            if (ImGui::Button("Empty"))
            {
                if (m_addEmptyEntityCallback)
                    m_addEmptyEntityCallback("Empty");
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Cube"))
            {
                if (m_addPrimitiveEntityCallback)
                    m_addPrimitiveEntityCallback("Cube");
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Sphere"))
            {
                if (m_addPrimitiveEntityCallback)
                    m_addPrimitiveEntityCallback("Sphere");

                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::EndPopup();
    }

}

void HierarchyPanel::drawHierarchyEntityNode(engine::Entity *entity)
{
    if (!entity)
        return;

    ImGui::PushID(static_cast<int>(entity->getId()));

    const bool selected = (entity == m_selectedEntity);
    const bool hasChildren = !entity->getChildren().empty();

    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
    if (!hasChildren)
        nodeFlags |= ImGuiTreeNodeFlags_Leaf;
    if (selected)
        nodeFlags |= ImGuiTreeNodeFlags_Selected;

    bool isEnabled = entity->isEnabled();
    if (ImGui::Checkbox("##EntityEnabled", &isEnabled))
        entity->setEnabled(isEnabled);

    ImGui::SameLine();

    std::string nodeLabel = entity->getName();
    if (!entity->isEnabled())
        nodeLabel += " (Disabled)";

    const bool nodeOpen = ImGui::TreeNodeEx(nodeLabel.c_str(), nodeFlags);

    if (ImGui::IsItemClicked())
        if (m_setSelectedEntityCallback)
            m_setSelectedEntityCallback(entity);

    if (ImGui::BeginDragDropSource())
    {
        const uint32_t entityId = entity->getId();
        ImGui::SetDragDropPayload("HIERARCHY_ENTITY_ID", &entityId, sizeof(entityId));
        ImGui::TextUnformatted(entity->getName().c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY_ID"))
        {
            const uint32_t draggedId = *static_cast<const uint32_t *>(payload->Data);
            if (auto *draggedEntity = m_scene ? m_scene->getEntityById(draggedId) : nullptr)
            {
                if (draggedEntity != entity && !entity->isDescendantOf(draggedEntity))
                {
                    glm::mat4 worldMatrix(1.0f);
                    if (auto *transform = draggedEntity->getComponent<engine::Transform3DComponent>())
                        worldMatrix = transform->getMatrix();

                    if (!draggedEntity->setParent(entity))
                        VX_EDITOR_WARNING_STREAM("Failed to parent entity '" << draggedEntity->getName() << "' under '" << entity->getName() << "'.");
                    else if (auto *transform = draggedEntity->getComponent<engine::Transform3DComponent>())
                    {
                        glm::mat4 localMatrix = worldMatrix;
                        if (auto *parentTransform = entity->getComponent<engine::Transform3DComponent>())
                            localMatrix = glm::inverse(parentTransform->getMatrix()) * worldMatrix;

                        glm::vec3 translation, rotation, scale;
                        ImGuizmo::DecomposeMatrixToComponents(
                            glm::value_ptr(localMatrix),
                            glm::value_ptr(translation),
                            glm::value_ptr(rotation),
                            glm::value_ptr(scale));

                        transform->setPosition(translation);
                        transform->setEulerDegrees(rotation);
                        transform->setScale(scale);
                    }
                }
            }
        }

        ImGui::EndDragDropTarget();
    }

    if (nodeOpen)
    {
        for (auto *child : entity->getChildren())
            drawHierarchyEntityNode(child);

        ImGui::TreePop();
    }

    ImGui::PopID();
}

ELIX_NESTED_NAMESPACE_END
