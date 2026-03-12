#include "Engine/SceneManager.hpp"

#include "Engine/Entity.hpp"
#include "Engine/Scene.hpp"
#include "Engine/Scripting/VelixAPI.hpp"

#include "Core/Logger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

SceneManager &SceneManager::instance()
{
    static SceneManager s_instance;
    return s_instance;
}

void SceneManager::requestLoadScene(const std::string &filePath)
{
    m_pendingRequests.push_back({Request::Type::LoadScene, filePath});
}

void SceneManager::requestLoadAdditive(const std::string &filePath)
{
    m_pendingRequests.push_back({Request::Type::LoadAdditive, filePath});
}

void SceneManager::requestUnloadGroup(const std::string &tag)
{
    m_pendingRequests.push_back({Request::Type::UnloadGroup, tag});
}

void SceneManager::setDontDestroyOnLoad(Entity *entity)
{
    if (entity)
        entity->addTag(k_dontDestroyTag);
}

void SceneManager::clearDontDestroyOnLoad(Entity *entity)
{
    if (entity)
        entity->removeTag(k_dontDestroyTag);
}

bool SceneManager::hasPendingRequests() const
{
    return !m_pendingRequests.empty();
}

void SceneManager::processRequests(std::shared_ptr<Scene> &activeScene,
                                   const SceneChangedCallback &onSceneChanged)
{
    if (m_pendingRequests.empty())
        return;

    auto requests = std::move(m_pendingRequests);
    m_pendingRequests.clear();

    for (const auto &request : requests)
    {
        switch (request.type)
        {
        case Request::Type::LoadScene:
        {
            if (!activeScene)
                break;

            // Preserve DontDestroyOnLoad entities before the new scene is created.
            auto preserved = activeScene->extractEntitiesWithTag(k_dontDestroyTag);

            auto newScene = std::make_shared<Scene>();
            if (!newScene->loadSceneFromFile(request.payload))
            {
                VX_ENGINE_ERROR_STREAM("SceneManager: failed to load scene: " << request.payload << '\n');
                // Roll back: re-inject preserved entities into the original scene.
                activeScene->injectEntities(std::move(preserved));
                break;
            }

            // Re-inject preserved entities into the new scene.
            newScene->injectEntities(std::move(preserved));

            activeScene = newScene;
            scripting::setActiveScene(activeScene.get());

            if (onSceneChanged)
                onSceneChanged(activeScene);

            VX_ENGINE_INFO_STREAM("SceneManager: loaded scene: " << request.payload << '\n');
            break;
        }

        case Request::Type::LoadAdditive:
        {
            if (!activeScene)
                break;

            if (!activeScene->loadEntitiesFromFile(request.payload))
                VX_ENGINE_ERROR_STREAM("SceneManager: failed to load additive scene: " << request.payload << '\n');
            else
                VX_ENGINE_INFO_STREAM("SceneManager: loaded additive scene: " << request.payload << '\n');
            break;
        }

        case Request::Type::UnloadGroup:
        {
            if (!activeScene)
                break;

            const auto removed = activeScene->extractEntitiesWithTag(request.payload);
            VX_ENGINE_INFO_STREAM("SceneManager: unloaded " << removed.size()
                                  << " entities with tag '" << request.payload << "'\n");
            break;
        }
        }
    }
}

ELIX_NESTED_NAMESPACE_END
