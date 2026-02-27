#include "Engine/Scripting/VelixAPI.hpp"

#include "Engine/Scene.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(scripting)

namespace
{
    Scene *g_activeScene{nullptr};
    platform::Window *g_activeWindow{nullptr};

    Scene *resolveScene(Scene *scene)
    {
        return scene ? scene : g_activeScene;
    }
} // namespace

void setActiveScene(Scene *scene)
{
    g_activeScene = scene;
}

Scene *const getActiveScene()
{
    return g_activeScene;
}

void setActiveWindow(platform::Window *window)
{
    g_activeWindow = window;
}

platform::Window *const getActiveWindow()
{
    return g_activeWindow;
}

Entity *spawnEntity(const char *name, Scene *scene)
{
    auto *targetScene = resolveScene(scene);

    if (!targetScene)
        return nullptr;

    const std::string entityName = name ? name : "Entity";
    auto entity = targetScene->addEntity(entityName);

    return entity ? entity.get() : nullptr;
}

bool destroyEntity(Entity *entity, Scene *scene)
{
    auto *targetScene = resolveScene(scene);

    if (!targetScene || !entity)
        return false;

    return targetScene->destroyEntity(entity);
}

Entity *findEntityById(uint32_t id, Scene *scene)
{
    auto *targetScene = resolveScene(scene);

    if (!targetScene)
        return nullptr;

    return targetScene->getEntityById(id);
}

uint64_t getEntitiesCount(Scene *scene)
{
    auto *targetScene = resolveScene(scene);

    if (!targetScene)
        return 0;

    return targetScene->getEntities().size();
}

Entity *getEntityByIndex(uint64_t index, Scene *scene)
{
    auto *targetScene = resolveScene(scene);

    if (!targetScene)
        return nullptr;

    const auto &entities = targetScene->getEntities();
    if (index >= entities.size())
        return nullptr;

    return entities[index] ? entities[index].get() : nullptr;
}

std::string getEntityName(const Entity *entity)
{
    if (!entity)
        return {};

    return entity->getName();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
