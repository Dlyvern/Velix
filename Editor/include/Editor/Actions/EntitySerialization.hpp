#ifndef ELIX_EDITOR_ACTIONS_ENTITY_SERIALIZATION_HPP
#define ELIX_EDITOR_ACTIONS_ENTITY_SERIALIZATION_HPP

#include "Core/Macros.hpp"
#include "Engine/Scene.hpp"

#include <cstdint>
#include <optional>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(editor)
namespace actions
{
    struct EntityHierarchySnapshot
    {
        std::string payload;
        std::uint32_t rootEntityId{0u};
        std::string rootEntityName;
    };

    std::optional<EntityHierarchySnapshot> captureEntityHierarchy(engine::Scene &scene, engine::Entity &rootEntity);
    engine::Entity *restoreEntityHierarchy(engine::Scene &scene,
                                           const std::string &payload,
                                           std::uint32_t *outRootEntityId = nullptr);
    bool prepareEntityHierarchyForPaste(engine::Scene &scene,
                                        std::string &payload,
                                        std::uint64_t pasteCounter,
                                        std::uint32_t *outNewRootEntityId = nullptr);
} // namespace actions
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_ACTIONS_ENTITY_SERIALIZATION_HPP
