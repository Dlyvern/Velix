#ifndef ELIX_EDITOR_ACTIONS_COMMANDS_DELETE_ENTITY_COMMAND_HPP
#define ELIX_EDITOR_ACTIONS_COMMANDS_DELETE_ENTITY_COMMAND_HPP

#include "Editor/Actions/EditorActionHistory.hpp"

#include <cstdint>
#include <memory>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(editor)
namespace actions
{
    class DeleteEntityCommand final : public IEditorCommand
    {
    public:
        DeleteEntityCommand(engine::Scene *scene,
                            std::string serializedHierarchy,
                            uint32_t rootEntityId,
                            std::string label);

        static std::unique_ptr<DeleteEntityCommand> capture(engine::Scene &scene,
                                                            engine::Entity &entity,
                                                            std::string label = "Delete Entity");

        bool execute() override;
        bool undo() override;
        const char *getName() const override;

    private:
        engine::Scene *m_scene{nullptr};
        std::string m_serializedHierarchy;
        uint32_t m_rootEntityId{0u};
        std::string m_label;
    };
} // namespace actions
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_ACTIONS_COMMANDS_DELETE_ENTITY_COMMAND_HPP
