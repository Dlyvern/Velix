#include "Editor/Actions/Commands/UIStateCommand.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)
namespace actions
{
    UIStateCommand::UIStateCommand(engine::Scene *scene,
                                   std::string beforeState,
                                   std::string afterState,
                                   std::string label)
        : m_scene(scene),
          m_beforeState(std::move(beforeState)),
          m_afterState(std::move(afterState)),
          m_label(std::move(label))
    {
    }

    bool UIStateCommand::execute()
    {
        return m_scene && m_scene->restoreUIState(m_afterState);
    }

    bool UIStateCommand::undo()
    {
        return m_scene && m_scene->restoreUIState(m_beforeState);
    }

    const char *UIStateCommand::getName() const
    {
        return m_label.c_str();
    }
} // namespace actions
ELIX_NESTED_NAMESPACE_END
