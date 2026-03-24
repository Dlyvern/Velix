#ifndef ELIX_EDITOR_ACTIONS_EDITOR_ACTION_HISTORY_HPP
#define ELIX_EDITOR_ACTIONS_EDITOR_ACTION_HISTORY_HPP

#include "Core/Macros.hpp"
#include "Engine/Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)
namespace actions
{
    class IEditorCommand
    {
    public:
        virtual ~IEditorCommand() = default;

        virtual bool execute() = 0;
        virtual bool undo() = 0;
        virtual const char *getName() const = 0;
    };

    class EditorCommandHistory
    {
    public:
        explicit EditorCommandHistory(std::size_t maxEntries = 64u);
        ~EditorCommandHistory();

        void clear();
        void setMaxEntries(std::size_t maxEntries);

        bool execute(std::unique_ptr<IEditorCommand> command);
        bool recordExecuted(std::unique_ptr<IEditorCommand> command);

        bool canUndo() const;
        bool canRedo() const;

        bool undo();
        bool redo();

    private:
        void trimHistoryToLimit();

    private:
        std::vector<std::unique_ptr<IEditorCommand>> m_commands;
        std::size_t m_nextCommandIndex{0u};
        std::size_t m_maxEntries{64u};
    };

    class EditorEntityClipboard
    {
    public:
        EditorEntityClipboard();
        ~EditorEntityClipboard();

        void clear();
        bool hasEntity() const;

        bool copySelectedEntity(engine::Scene &scene, std::uint32_t entityId);
        bool pasteEntity(engine::Scene &scene, std::uint32_t *outNewEntityId = nullptr);

    private:
        std::string m_serializedEntityHierarchy;
        std::uint64_t m_pasteCounter{0u};
    };
} // namespace actions
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_ACTIONS_EDITOR_ACTION_HISTORY_HPP
