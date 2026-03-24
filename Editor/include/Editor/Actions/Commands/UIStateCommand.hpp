#ifndef ELIX_EDITOR_ACTIONS_COMMANDS_UI_STATE_COMMAND_HPP
#define ELIX_EDITOR_ACTIONS_COMMANDS_UI_STATE_COMMAND_HPP

#include "Editor/Actions/EditorActionHistory.hpp"

#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(editor)
namespace actions
{
    class UIStateCommand final : public IEditorCommand
    {
    public:
        UIStateCommand(engine::Scene *scene,
                       std::string beforeState,
                       std::string afterState,
                       std::string label);

        bool execute() override;
        bool undo() override;
        const char *getName() const override;

    private:
        engine::Scene *m_scene{nullptr};
        std::string m_beforeState;
        std::string m_afterState;
        std::string m_label;
    };
} // namespace actions
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_ACTIONS_COMMANDS_UI_STATE_COMMAND_HPP
