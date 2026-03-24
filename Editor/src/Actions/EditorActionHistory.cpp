#include "Editor/Actions/EditorActionHistory.hpp"

#include "Core/Logger.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <unordered_map>
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

        bool prepareSerializedHierarchyForPaste(engine::Scene &scene,
                                                const std::string &serializedHierarchy,
                                                std::uint64_t pasteCounter,
                                                std::string &outPayload,
                                                std::uint32_t *outRootEntityId)
        {
            outPayload.clear();
            if (outRootEntityId)
                *outRootEntityId = 0u;

            nlohmann::json json;
            try
            {
                json = nlohmann::json::parse(serializedHierarchy);
            }
            catch (const std::exception &)
            {
                return false;
            }

            if (!json.is_object() || !json.contains("game_objects") || !json["game_objects"].is_array() || json["game_objects"].empty())
                return false;

            auto &gameObjectsJson = json["game_objects"];

            const bool hasExplicitRootId = json.contains("root_id") && json["root_id"].is_number_unsigned();
            std::uint32_t sourceRootId = 0u;
            bool rootIdInitialized = false;

            std::vector<std::uint32_t> sourceIds;
            sourceIds.reserve(gameObjectsJson.size());

            std::unordered_set<std::uint32_t> uniqueSourceIds;
            for (const auto &objectJson : gameObjectsJson)
            {
                if (!objectJson.is_object() || !objectJson.contains("id") || !objectJson["id"].is_number_unsigned())
                    return false;

                const std::uint32_t sourceId = objectJson["id"].get<std::uint32_t>();
                if (!uniqueSourceIds.insert(sourceId).second)
                    return false;

                sourceIds.push_back(sourceId);

                if ((!hasExplicitRootId && !rootIdInitialized) ||
                    (hasExplicitRootId && sourceId == json["root_id"].get<std::uint32_t>()))
                {
                    sourceRootId = sourceId;
                    rootIdInitialized = true;
                }
            }

            if (!rootIdInitialized)
                return false;

            std::unordered_set<std::uint32_t> occupiedIds;
            std::uint64_t nextIdCandidate = 0u;
            for (const auto &entity : scene.getEntities())
            {
                if (!entity)
                    continue;

                occupiedIds.insert(entity->getId());
                nextIdCandidate = std::max(nextIdCandidate, static_cast<std::uint64_t>(entity->getId()) + 1u);
            }

            auto allocateEntityId = [&]() -> std::optional<std::uint32_t>
            {
                while (nextIdCandidate <= std::numeric_limits<std::uint32_t>::max() &&
                       occupiedIds.contains(static_cast<std::uint32_t>(nextIdCandidate)))
                {
                    ++nextIdCandidate;
                }

                if (nextIdCandidate > std::numeric_limits<std::uint32_t>::max())
                    return std::nullopt;

                const std::uint32_t allocatedId = static_cast<std::uint32_t>(nextIdCandidate);
                occupiedIds.insert(allocatedId);
                ++nextIdCandidate;
                return allocatedId;
            };

            std::unordered_map<std::uint32_t, std::uint32_t> remappedIds;
            remappedIds.reserve(sourceIds.size());
            for (const std::uint32_t sourceId : sourceIds)
            {
                const auto remappedId = allocateEntityId();
                if (!remappedId.has_value())
                    return false;

                remappedIds.emplace(sourceId, remappedId.value());
            }

            std::unordered_set<std::string> existingNames;
            existingNames.reserve(scene.getEntities().size());
            for (const auto &entity : scene.getEntities())
            {
                if (entity)
                    existingNames.insert(entity->getName());
            }

            for (auto &objectJson : gameObjectsJson)
            {
                const std::uint32_t sourceId = objectJson["id"].get<std::uint32_t>();
                objectJson["id"] = remappedIds.at(sourceId);

                if (objectJson.contains("parent_id") && objectJson["parent_id"].is_number_unsigned())
                {
                    const std::uint32_t sourceParentId = objectJson["parent_id"].get<std::uint32_t>();
                    if (auto remappedParentIt = remappedIds.find(sourceParentId); remappedParentIt != remappedIds.end())
                        objectJson["parent_id"] = remappedParentIt->second;
                    else if (!scene.getEntityById(sourceParentId))
                        objectJson.erase("parent_id");
                }

                if (sourceId == sourceRootId)
                {
                    const std::string originalName = objectJson.value("name", std::string("Entity"));
                    const std::string uniqueName = makeUniqueName(originalName, existingNames);
                    objectJson["name"] = uniqueName;
                    existingNames.insert(uniqueName);

                    if (objectJson.contains("position") &&
                        objectJson["position"].is_array() &&
                        objectJson["position"].size() == 3 &&
                        objectJson["position"][0].is_number() &&
                        objectJson["position"][2].is_number())
                    {
                        auto &positionJson = objectJson["position"];
                        const float offset = 0.35f + static_cast<float>(pasteCounter % 6u) * 0.1f;
                        positionJson[0] = positionJson[0].get<float>() + offset;
                        positionJson[2] = positionJson[2].get<float>() + offset;
                    }
                }
            }

            const std::uint32_t remappedRootId = remappedIds.at(sourceRootId);
            json["root_id"] = remappedRootId;
            outPayload = json.dump();

            if (outRootEntityId)
                *outRootEntityId = remappedRootId;

            return true;
        }
    } // namespace

    EditorCommandHistory::EditorCommandHistory(std::size_t maxEntries)
        : m_maxEntries(std::max<std::size_t>(maxEntries, 1u))
    {
    }

    EditorCommandHistory::~EditorCommandHistory() = default;

    void EditorCommandHistory::clear()
    {
        m_commands.clear();
        m_nextCommandIndex = 0u;
    }

    void EditorCommandHistory::setMaxEntries(std::size_t maxEntries)
    {
        m_maxEntries = std::max<std::size_t>(maxEntries, 1u);
        trimHistoryToLimit();
    }

    bool EditorCommandHistory::execute(std::unique_ptr<IEditorCommand> command)
    {
        if (!command)
            return false;

        if (m_nextCommandIndex < m_commands.size())
            m_commands.erase(m_commands.begin() + static_cast<std::ptrdiff_t>(m_nextCommandIndex), m_commands.end());

        if (!command->execute())
        {
            VX_EDITOR_WARNING_STREAM("Failed to execute editor command '" << command->getName() << "'.\n");
            return false;
        }

        m_commands.push_back(std::move(command));
        m_nextCommandIndex = m_commands.size();
        trimHistoryToLimit();
        return true;
    }

    bool EditorCommandHistory::recordExecuted(std::unique_ptr<IEditorCommand> command)
    {
        if (!command)
            return false;

        if (m_nextCommandIndex < m_commands.size())
            m_commands.erase(m_commands.begin() + static_cast<std::ptrdiff_t>(m_nextCommandIndex), m_commands.end());

        m_commands.push_back(std::move(command));
        m_nextCommandIndex = m_commands.size();
        trimHistoryToLimit();
        return true;
    }

    bool EditorCommandHistory::canUndo() const
    {
        return m_nextCommandIndex > 0u && !m_commands.empty();
    }

    bool EditorCommandHistory::canRedo() const
    {
        return m_nextCommandIndex < m_commands.size();
    }

    bool EditorCommandHistory::undo()
    {
        if (!canUndo())
            return false;

        const std::size_t commandIndex = m_nextCommandIndex - 1u;
        if (!m_commands[commandIndex]->undo())
        {
            VX_EDITOR_WARNING_STREAM("Failed to undo editor command '" << m_commands[commandIndex]->getName() << "'.\n");
            return false;
        }

        m_nextCommandIndex = commandIndex;
        return true;
    }

    bool EditorCommandHistory::redo()
    {
        if (!canRedo())
            return false;

        if (!m_commands[m_nextCommandIndex]->execute())
        {
            VX_EDITOR_WARNING_STREAM("Failed to redo editor command '" << m_commands[m_nextCommandIndex]->getName() << "'.\n");
            return false;
        }

        ++m_nextCommandIndex;
        return true;
    }

    void EditorCommandHistory::trimHistoryToLimit()
    {
        while (m_commands.size() > m_maxEntries)
        {
            m_commands.erase(m_commands.begin());
            if (m_nextCommandIndex > 0u)
                --m_nextCommandIndex;
        }
    }

    EditorEntityClipboard::EditorEntityClipboard() = default;

    EditorEntityClipboard::~EditorEntityClipboard() = default;

    void EditorEntityClipboard::clear()
    {
        m_serializedEntityHierarchy.clear();
    }

    bool EditorEntityClipboard::hasEntity() const
    {
        return !m_serializedEntityHierarchy.empty();
    }

    bool EditorEntityClipboard::copySelectedEntity(engine::Scene &scene, std::uint32_t entityId)
    {
        return scene.serializeEntityHierarchy(entityId, m_serializedEntityHierarchy);
    }

    bool EditorEntityClipboard::pasteEntity(engine::Scene &scene, std::uint32_t *outNewEntityId)
    {
        if (!hasEntity())
            return false;

        std::string preparedPayload;
        std::uint32_t expectedRootEntityId = 0u;
        if (!prepareSerializedHierarchyForPaste(scene,
                                                m_serializedEntityHierarchy,
                                                m_pasteCounter,
                                                preparedPayload,
                                                &expectedRootEntityId))
        {
            return false;
        }

        std::uint32_t restoredRootEntityId = 0u;
        engine::Entity *entity = scene.restoreEntityHierarchy(preparedPayload, &restoredRootEntityId);
        if (!entity)
            return false;

        ++m_pasteCounter;
        if (outNewEntityId)
            *outNewEntityId = restoredRootEntityId != 0u ? restoredRootEntityId : expectedRootEntityId;

        return true;
    }
} // namespace actions
ELIX_NESTED_NAMESPACE_END
