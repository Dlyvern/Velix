#ifndef ELIX_SCENE_HPP
#define ELIX_SCENE_HPP

#include "Core/Macros.hpp"

#include "Engine/Entity.hpp"
#include "Engine/Lights.hpp"

#include <vector>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Scene
{
public:
    using SharedPtr = std::shared_ptr<Scene>;
    Scene();
    ~Scene();

    const std::vector<Entity::SharedPtr>& getEntities() const;

    std::vector<std::shared_ptr<BaseLight>> getLights() const;

    Entity::SharedPtr addEntity(const std::string& name);
    void destroyEntity(Entity::SharedPtr entity);

    bool loadSceneFromFile(const std::string& filePath);
    void saveSceneToFile(const std::string& filePath);

    void update(float deltaTime);
    void fixedUpdate(float fixedDelta);
private:
    std::vector<Entity::SharedPtr> m_entities;
    std::string m_name;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_SCENE_HPP