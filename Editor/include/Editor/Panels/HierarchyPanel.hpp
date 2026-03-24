#ifndef ELIX_HIERARCHY_PANEL_HPP
#define ELIX_HIERARCHY_PANEL_HPP

#include "Core/Macros.hpp"

#include "Engine/Entity.hpp"
#include "Engine/Scene.hpp"

#include <functional>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class HierarchyPanel
{
public:
    void drawContents();

    void setSetSelectedEntityCallback(const std::function<void(engine::Entity *)> &function);
    void setAddEmptyEntityCallback(const std::function<void(const std::string &)> &function);
    void setAddPrimitiveEntityCallback(const std::function<void(const std::string &)> &function);

    void setSelectedEntity(engine::Entity *entity);
    void setScene(engine::Scene *scene);

private:
    void drawHierarchyEntityNode(engine::Entity *entity);

    std::function<void(engine::Entity *)> m_setSelectedEntityCallback{nullptr};
    std::function<void(const std::string &)> m_addEmptyEntityCallback{nullptr};
    std::function<void(const std::string &)> m_addPrimitiveEntityCallback{nullptr};

    engine::Entity *m_selectedEntity{nullptr};
    engine::Scene *m_scene{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_HIERARCHY_PANEL_HPP