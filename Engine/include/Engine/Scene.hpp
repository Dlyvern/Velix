#ifndef ELIX_SCENE_HPP
#define ELIX_SCENE_HPP

#include "Core/Macros.hpp"

#include "Engine/Entity.hpp"
#include "Engine/Lights.hpp"

#include "Engine/Physics/PhysicsScene.hpp"

#include <vector>
#include <memory>
#include <unordered_map>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Scene
{
public:
    using SharedPtr = std::shared_ptr<Scene>;
    Scene();
    ~Scene();

    Scene::SharedPtr copy();

    const std::vector<Entity::SharedPtr> &getEntities() const;

    std::vector<std::shared_ptr<BaseLight>> getLights();

    bool doesEntityNameExist(const std::string &name) const;

    Entity::SharedPtr
    addEntity(const std::string &name);
    Entity::SharedPtr addEntity(Entity &en, const std::string &name);

    Entity *getEntityById(uint32_t id);

    bool destroyEntity(Entity *entity);

    bool loadSceneFromFile(const std::string &filePath);
    void saveSceneToFile(const std::string &filePath);

    void setSkyboxHDRPath(const std::string &path);
    const std::string &getSkyboxHDRPath() const;
    bool hasSkyboxHDR() const;
    void clearSkyboxHDR();

    void update(float deltaTime);
    void fixedUpdate(float fixedDelta);

    PhysicsScene &getPhysicsScene();

private:
    std::vector<Entity::SharedPtr> m_entities;
    std::string m_name;
    PhysicsScene m_physicsScene;
    uint64_t m_nextEntityId{0}; // TODO fix it
    std::string m_skyboxHDRPath;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SCENE_HPP
