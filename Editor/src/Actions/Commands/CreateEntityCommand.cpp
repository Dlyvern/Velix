#include "Editor/Actions/Commands/CreateEntityCommand.hpp"

#include "Core/Logger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)
namespace actions
{
    CreateEntityCommand::CreateEntityCommand(engine::Scene *scene,
                                             std::string serializedHierarchy,
                                             uint32_t rootEntityId,
                                             std::string label)
        : m_scene(scene),
          m_serializedHierarchy(std::move(serializedHierarchy)),
          m_rootEntityId(rootEntityId),
          m_label(std::move(label))
    {
    }

    std::unique_ptr<CreateEntityCommand> CreateEntityCommand::capture(engine::Scene &scene,
                                                                      engine::Entity &entity,
                                                                      std::string label)
    {
        std::string serializedHierarchy;
        if (!scene.serializeEntityHierarchy(entity.getId(), serializedHierarchy))
            return nullptr;

        return std::make_unique<CreateEntityCommand>(&scene, std::move(serializedHierarchy), entity.getId(), std::move(label));
    }

    bool CreateEntityCommand::execute()
    {
        if (!m_scene)
            return false;

        uint32_t restoredRootEntityId = 0u;
        engine::Entity *entity = m_scene->restoreEntityHierarchy(m_serializedHierarchy, &restoredRootEntityId);
        if (!entity)
        {
            VX_EDITOR_WARNING_STREAM("CreateEntityCommand failed to restore entity hierarchy for '" << m_label << "'.\n");
            return false;
        }

        m_rootEntityId = restoredRootEntityId;
        return true;
    }

    bool CreateEntityCommand::undo()
    {
        if (!m_scene)
            return false;

        engine::Entity *entity = m_scene->getEntityById(m_rootEntityId);
        if (!entity)
            return false;

        return m_scene->destroyEntity(entity);
    }

    const char *CreateEntityCommand::getName() const
    {
        return m_label.c_str();
    }

    uint32_t CreateEntityCommand::getRootEntityId() const
    {
        return m_rootEntityId;
    }
} // namespace actions
ELIX_NESTED_NAMESPACE_END
