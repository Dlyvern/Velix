#include "Editor/Actions/EditorActionHistory.hpp"

#include "Core/Logger.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>
#include <unordered_set>

ELIX_NESTED_NAMESPACE_BEGIN(editor)
namespace actions
{
    namespace
    {
        std::string makeUniqueName(const std::string &baseName, const std::unordered_set<std::string> &existingNames)
        {
            if (!existingNames.contains(baseName))
                return baseName;

            const std::string copyBase = baseName + "_Copy";
            if (!existingNames.contains(copyBase))
                return copyBase;

            std::uint32_t index = 1u;
            while (true)
            {
                const std::string candidate = copyBase + "_" + std::to_string(index);
                if (!existingNames.contains(candidate))
                    return candidate;

                ++index;
            }
        }
    } // namespace

    EditorSceneHistory::EditorSceneHistory(std::size_t maxEntries)
        : m_maxEntries(std::max<std::size_t>(maxEntries, 2u))
    {
    }

    EditorSceneHistory::~EditorSceneHistory()
    {
        clear();
    }

    void EditorSceneHistory::clear()
    {
        for (const auto &entry : m_entries)
            eraseEntryFile(entry);

        m_entries.clear();
        m_currentIndex = 0u;

        if (!m_snapshotDirectory.empty())
        {
            std::error_code removeError;
            std::filesystem::remove_all(m_snapshotDirectory, removeError);
            m_snapshotDirectory.clear();
        }
    }

    void EditorSceneHistory::setMaxEntries(std::size_t maxEntries)
    {
        m_maxEntries = std::max<std::size_t>(maxEntries, 2u);
        trimHistoryToLimit();
    }

    bool EditorSceneHistory::reset(engine::Scene &scene, const std::string &label)
    {
        clear();
        SnapshotEntry snapshot;
        if (!saveSnapshot(scene, snapshot, label))
            return false;

        m_entries.push_back(std::move(snapshot));
        m_currentIndex = 0u;
        return true;
    }

    bool EditorSceneHistory::capture(engine::Scene &scene, const std::string &label)
    {
        if (m_entries.empty())
            return reset(scene, label);

        while (m_entries.size() > (m_currentIndex + 1u))
        {
            eraseEntryFile(m_entries.back());
            m_entries.pop_back();
        }

        SnapshotEntry snapshot;
        if (!saveSnapshot(scene, snapshot, label))
            return false;

        m_entries.push_back(std::move(snapshot));
        m_currentIndex = m_entries.size() - 1u;

        trimHistoryToLimit();
        return true;
    }

    bool EditorSceneHistory::canUndo() const
    {
        return !m_entries.empty() && m_currentIndex > 0u;
    }

    bool EditorSceneHistory::canRedo() const
    {
        return !m_entries.empty() && (m_currentIndex + 1u) < m_entries.size();
    }

    bool EditorSceneHistory::undo(engine::Scene &scene)
    {
        if (!canUndo())
            return false;

        const std::size_t targetIndex = m_currentIndex - 1u;
        if (!restoreSnapshot(scene, m_entries[targetIndex]))
            return false;

        m_currentIndex = targetIndex;
        return true;
    }

    bool EditorSceneHistory::redo(engine::Scene &scene)
    {
        if (!canRedo())
            return false;

        const std::size_t targetIndex = m_currentIndex + 1u;
        if (!restoreSnapshot(scene, m_entries[targetIndex]))
            return false;

        m_currentIndex = targetIndex;
        return true;
    }

    bool EditorSceneHistory::saveSnapshot(engine::Scene &scene, SnapshotEntry &outEntry, const std::string &label)
    {
        ensureSnapshotDirectory();
        if (m_snapshotDirectory.empty())
            return false;

        outEntry.path = makeSnapshotPath("history");
        outEntry.label = label;

        scene.saveSceneToFile(outEntry.path.string());
        if (!std::filesystem::exists(outEntry.path))
        {
            VX_EDITOR_ERROR_STREAM("Failed to create editor history snapshot: " << outEntry.path.string() << '\n');
            return false;
        }

        return true;
    }

    bool EditorSceneHistory::restoreSnapshot(engine::Scene &scene, const SnapshotEntry &entry) const
    {
        if (!std::filesystem::exists(entry.path))
        {
            VX_EDITOR_ERROR_STREAM("Editor history snapshot is missing: " << entry.path.string() << '\n');
            return false;
        }

        if (!scene.loadSceneFromFile(entry.path.string()))
        {
            VX_EDITOR_ERROR_STREAM("Failed to restore editor history snapshot: " << entry.path.string() << '\n');
            return false;
        }

        return true;
    }

    std::filesystem::path EditorSceneHistory::makeSnapshotPath(const std::string &prefix)
    {
        ensureSnapshotDirectory();
        const std::string filename = prefix + "_" + std::to_string(++m_snapshotCounter) + ".elixscene";
        return m_snapshotDirectory / filename;
    }

    void EditorSceneHistory::ensureSnapshotDirectory()
    {
        if (!m_snapshotDirectory.empty())
            return;

        std::error_code tempError;
        std::filesystem::path tempRoot = std::filesystem::temp_directory_path(tempError);
        if (tempError)
            tempRoot = std::filesystem::current_path();

        const std::string directoryName = "velix_editor_history_" +
                                          std::to_string(reinterpret_cast<std::uintptr_t>(this));
        m_snapshotDirectory = tempRoot / directoryName;

        std::error_code createError;
        std::filesystem::create_directories(m_snapshotDirectory, createError);
        if (createError)
        {
            VX_EDITOR_ERROR_STREAM("Failed to create editor history directory '" << m_snapshotDirectory.string()
                                                                                 << "': " << createError.message() << '\n');
            m_snapshotDirectory.clear();
        }
    }

    void EditorSceneHistory::eraseEntryFile(const SnapshotEntry &entry) const
    {
        if (entry.path.empty())
            return;

        std::error_code removeError;
        std::filesystem::remove(entry.path, removeError);
    }

    void EditorSceneHistory::trimHistoryToLimit()
    {
        while (m_entries.size() > m_maxEntries)
        {
            eraseEntryFile(m_entries.front());
            m_entries.erase(m_entries.begin());
            if (m_currentIndex > 0u)
                --m_currentIndex;
        }
    }

    EditorEntityClipboard::EditorEntityClipboard() = default;

    EditorEntityClipboard::~EditorEntityClipboard()
    {
        if (!m_tempDirectory.empty())
        {
            std::error_code removeError;
            std::filesystem::remove_all(m_tempDirectory, removeError);
            m_tempDirectory.clear();
        }
    }

    void EditorEntityClipboard::clear()
    {
        m_serializedEntityObject.clear();
    }

    bool EditorEntityClipboard::hasEntity() const
    {
        return !m_serializedEntityObject.empty();
    }

    bool EditorEntityClipboard::copySelectedEntity(engine::Scene &scene, std::uint32_t entityId)
    {
        ensureTempDirectory();
        if (m_tempDirectory.empty())
            return false;

        const std::filesystem::path tempScenePath = makeTempScenePath("copy");
        scene.saveSceneToFile(tempScenePath.string());

        std::ifstream sceneFile(tempScenePath);
        if (!sceneFile.is_open())
            return false;

        nlohmann::json sceneJson;
        try
        {
            sceneFile >> sceneJson;
        }
        catch (const std::exception &)
        {
            std::error_code removeError;
            std::filesystem::remove(tempScenePath, removeError);
            return false;
        }

        std::error_code removeError;
        std::filesystem::remove(tempScenePath, removeError);

        if (!sceneJson.contains("game_objects") || !sceneJson["game_objects"].is_array())
            return false;

        for (const auto &objectJson : sceneJson["game_objects"])
        {
            if (!objectJson.is_object())
                continue;

            if (!objectJson.contains("id") || !objectJson["id"].is_number_unsigned())
                continue;

            if (objectJson["id"].get<std::uint32_t>() != entityId)
                continue;

            m_serializedEntityObject = objectJson.dump();
            return true;
        }

        return false;
    }

    bool EditorEntityClipboard::pasteEntity(engine::Scene &scene, std::uint32_t *outNewEntityId)
    {
        if (!hasEntity())
            return false;

        ensureTempDirectory();
        if (m_tempDirectory.empty())
            return false;

        nlohmann::json copiedEntityJson;
        try
        {
            copiedEntityJson = nlohmann::json::parse(m_serializedEntityObject);
        }
        catch (const std::exception &)
        {
            return false;
        }

        const std::filesystem::path tempScenePath = makeTempScenePath("paste");
        scene.saveSceneToFile(tempScenePath.string());

        std::ifstream sceneFile(tempScenePath);
        if (!sceneFile.is_open())
            return false;

        nlohmann::json sceneJson;
        try
        {
            sceneFile >> sceneJson;
        }
        catch (const std::exception &)
        {
            std::error_code removeError;
            std::filesystem::remove(tempScenePath, removeError);
            return false;
        }

        if (!sceneJson.contains("game_objects") || !sceneJson["game_objects"].is_array())
            sceneJson["game_objects"] = nlohmann::json::array();

        std::unordered_set<std::uint32_t> existingIds;
        std::unordered_set<std::string> existingNames;
        std::uint32_t maxEntityId = 0u;
        bool hasEntityIds = false;

        for (const auto &objectJson : sceneJson["game_objects"])
        {
            if (!objectJson.is_object())
                continue;

            if (objectJson.contains("id") && objectJson["id"].is_number_unsigned())
            {
                const std::uint32_t id = objectJson["id"].get<std::uint32_t>();
                existingIds.insert(id);
                maxEntityId = std::max(maxEntityId, id);
                hasEntityIds = true;
            }

            if (objectJson.contains("name") && objectJson["name"].is_string())
                existingNames.insert(objectJson["name"].get<std::string>());
        }

        const std::uint32_t newEntityId = hasEntityIds ? (maxEntityId + 1u) : 0u;
        copiedEntityJson["id"] = newEntityId;

        const std::string originalName = copiedEntityJson.value("name", std::string("Entity"));
        copiedEntityJson["name"] = makeUniqueName(originalName, existingNames);

        if (copiedEntityJson.contains("parent_id") && copiedEntityJson["parent_id"].is_number_unsigned())
        {
            const std::uint32_t parentId = copiedEntityJson["parent_id"].get<std::uint32_t>();
            if (!existingIds.contains(parentId))
                copiedEntityJson.erase("parent_id");
        }

        if (copiedEntityJson.contains("position") && copiedEntityJson["position"].is_array() && copiedEntityJson["position"].size() == 3)
        {
            auto &positionJson = copiedEntityJson["position"];
            if (positionJson[0].is_number() && positionJson[2].is_number())
            {
                const float offset = 0.35f + static_cast<float>((m_pasteCounter % 6u)) * 0.1f;
                positionJson[0] = positionJson[0].get<float>() + offset;
                positionJson[2] = positionJson[2].get<float>() + offset;
            }
        }

        sceneJson["game_objects"].push_back(copiedEntityJson);

        std::ofstream outputFile(tempScenePath, std::ios::trunc);
        if (!outputFile.is_open())
            return false;
        outputFile << sceneJson.dump(4);
        outputFile.close();

        const bool loadResult = scene.loadSceneFromFile(tempScenePath.string());

        std::error_code removeError;
        std::filesystem::remove(tempScenePath, removeError);

        if (!loadResult)
            return false;

        ++m_pasteCounter;
        if (outNewEntityId)
            *outNewEntityId = newEntityId;

        return true;
    }

    std::filesystem::path EditorEntityClipboard::makeTempScenePath(const std::string &prefix)
    {
        ensureTempDirectory();
        const std::string filename = prefix + "_" + std::to_string(++m_tempCounter) + ".elixscene";
        return m_tempDirectory / filename;
    }

    void EditorEntityClipboard::ensureTempDirectory()
    {
        if (!m_tempDirectory.empty())
            return;

        std::error_code tempError;
        std::filesystem::path tempRoot = std::filesystem::temp_directory_path(tempError);
        if (tempError)
            tempRoot = std::filesystem::current_path();

        const std::string directoryName = "velix_editor_clipboard_" +
                                          std::to_string(reinterpret_cast<std::uintptr_t>(this));
        m_tempDirectory = tempRoot / directoryName;

        std::error_code createError;
        std::filesystem::create_directories(m_tempDirectory, createError);
        if (createError)
            m_tempDirectory.clear();
    }
} // namespace actions
ELIX_NESTED_NAMESPACE_END
