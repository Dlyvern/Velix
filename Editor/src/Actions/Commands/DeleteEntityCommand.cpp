#include "Editor/Actions/Commands/DeleteEntityCommand.hpp"

#include "Core/Logger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)
namespace actions
{
    DeleteEntityCommand::DeleteEntityCommand(engine::Scene *scene,
                                             std::string serializedHierarchy,
                                             uint32_t rootEntityId,
                                             std::string label)
        : m_scene(scene),
          m_serializedHierarchy(std::move(serializedHierarchy)),
          m_rootEntityId(rootEntityId),
          m_label(std::move(label))
    {
    }

    std::unique_ptr<DeleteEntityCommand> DeleteEntityCommand::capture(engine::Scene &scene,
                                                                      engine::Entity &entity,
                                                                      std::string label)
    {
        std::string serializedHierarchy;
        if (!scene.serializeEntityHierarchy(entity.getId(), serializedHierarchy))
            return nullptr;

        return std::make_unique<DeleteEntityCommand>(&scene, std::move(serializedHierarchy), entity.getId(), std::move(label));
    }

    bool DeleteEntityCommand::execute()
    {
        if (!m_scene)
            return false;

        engine::Entity *entity = m_scene->getEntityById(m_rootEntityId);
        if (!entity)
            return false;

        return m_scene->destroyEntity(entity);
    }

    bool DeleteEntityCommand::undo()
    {
        if (!m_scene)
            return false;

        uint32_t restoredRootEntityId = 0u;
        engine::Entity *entity = m_scene->restoreEntityHierarchy(m_serializedHierarchy, &restoredRootEntityId);
        if (!entity)
        {
            VX_EDITOR_WARNING_STREAM("DeleteEntityCommand failed to restore entity hierarchy for '" << m_label << "'.\n");
            return false;
        }

        m_rootEntityId = restoredRootEntityId;
        return true;
    }

    const char *DeleteEntityCommand::getName() const
    {
        return m_label.c_str();
    }
} // namespace actions
ELIX_NESTED_NAMESPACE_END
