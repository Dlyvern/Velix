#ifndef ELIX_SCENE_MANAGER_HPP
#define ELIX_SCENE_MANAGER_HPP

#include "Core/Macros.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Scene;
class Entity;

class SceneManager
{
public:
    static SceneManager &instance();

    void requestLoadScene(const std::string &filePath);

    void requestLoadAdditive(const std::string &filePath);

    void requestUnloadGroup(const std::string &tag);

    void setDontDestroyOnLoad(Entity *entity);
    void clearDontDestroyOnLoad(Entity *entity);

    bool hasPendingRequests() const;

    using SceneChangedCallback = std::function<void(std::shared_ptr<Scene>)>;
    void processRequests(std::shared_ptr<Scene> &activeScene,
                         const SceneChangedCallback &onSceneChanged = {});

private:
    SceneManager() = default;

    static constexpr const char *k_dontDestroyTag = "__dontdestroy__";

    struct Request
    {
        enum class Type
        {
            LoadScene,
            LoadAdditive,
            UnloadGroup
        } type;
        std::string payload;
    };

    std::vector<Request> m_pendingRequests;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SCENE_MANAGER_HPP
