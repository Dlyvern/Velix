#ifndef ELIX_EDITOR_ACTIONS_COMMANDS_CREATE_ENTITY_COMMAND_HPP
#define ELIX_EDITOR_ACTIONS_COMMANDS_CREATE_ENTITY_COMMAND_HPP

#include "Editor/Actions/EditorActionHistory.hpp"

#include <cstdint>
#include <memory>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(editor)
namespace actions
{
    class CreateEntityCommand final : public IEditorCommand
    {
    public:
        CreateEntityCommand(engine::Scene *scene,
                            std::string serializedHierarchy,
                            uint32_t rootEntityId,
                            std::string label);

        static std::unique_ptr<CreateEntityCommand> capture(engine::Scene &scene,
                                                            engine::Entity &entity,
                                                            std::string label = "Create Entity");

        bool execute() override;
        bool undo() override;
        const char *getName() const override;

        uint32_t getRootEntityId() const;

    private:
        engine::Scene *m_scene{nullptr};
        std::string m_serializedHierarchy;
        uint32_t m_rootEntityId{0u};
        std::string m_label;
    };
} // namespace actions
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_ACTIONS_COMMANDS_CREATE_ENTITY_COMMAND_HPP
