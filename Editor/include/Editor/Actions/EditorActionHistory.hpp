#ifndef ELIX_EDITOR_ACTIONS_EDITOR_ACTION_HISTORY_HPP
#define ELIX_EDITOR_ACTIONS_EDITOR_ACTION_HISTORY_HPP

#include "Core/Macros.hpp"
#include "Engine/Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)
namespace actions
{
    class EditorSceneHistory
    {
    public:
        explicit EditorSceneHistory(std::size_t maxEntries = 64u);
        ~EditorSceneHistory();

        void clear();
        void setMaxEntries(std::size_t maxEntries);

        bool reset(engine::Scene &scene, const std::string &label = "Initial state");
        bool capture(engine::Scene &scene, const std::string &label);

        bool canUndo() const;
        bool canRedo() const;

        bool undo(engine::Scene &scene);
        bool redo(engine::Scene &scene);

    private:
        struct SnapshotEntry
        {
            std::filesystem::path path;
            std::string label;
        };

        bool saveSnapshot(engine::Scene &scene, SnapshotEntry &outEntry, const std::string &label);
        bool restoreSnapshot(engine::Scene &scene, const SnapshotEntry &entry) const;
        std::filesystem::path makeSnapshotPath(const std::string &prefix);
        void ensureSnapshotDirectory();
        void eraseEntryFile(const SnapshotEntry &entry) const;
        void trimHistoryToLimit();

    private:
        std::vector<SnapshotEntry> m_entries;
        std::size_t m_currentIndex{0u};
        std::size_t m_maxEntries{64u};
        std::filesystem::path m_snapshotDirectory;
        std::uint64_t m_snapshotCounter{0u};
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
        std::filesystem::path makeTempScenePath(const std::string &prefix);
        void ensureTempDirectory();

    private:
        std::string m_serializedEntityObject;
        std::filesystem::path m_tempDirectory;
        std::uint64_t m_tempCounter{0u};
        std::uint64_t m_pasteCounter{0u};
    };
} // namespace actions
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_ACTIONS_EDITOR_ACTION_HISTORY_HPP
