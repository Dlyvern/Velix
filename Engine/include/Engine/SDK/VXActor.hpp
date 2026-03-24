#ifndef ELIX_VX_ACTOR_HPP
#define ELIX_VX_ACTOR_HPP

#include "Engine/SDK/VXObject.hpp"
#include "Engine/Scripting/Script.hpp"
#include "Engine/Entity.hpp"
#include "Engine/Scene.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Components/ScriptComponent.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <typeinfo>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

// VXActor is the standard base class for game logic in Velix.
// Attach to an entity by registering with REGISTER_SCRIPT(MyActor).
// Replaces direct Script subclassing with a higher-level, friendlier API.
class VXActor : public VXObject, public Script
{
public:
    void onStart() override {}
    void onUpdate(float /*dt*/) override {}
    void onStop() override {}
    void onCollisionEnter(Entity *other, const CollisionInfo &) override {}
    void onCollisionExit(Entity *other) override {}
    void onTriggerEnter(Entity *other) override {}
    void onTriggerExit(Entity *other) override {}

    Transform3DComponent &getTransform();

    glm::vec3 getPosition() const;
    void setPosition(glm::vec3 pos);
    glm::quat getRotation() const;
    void setRotation(glm::quat rot);
    glm::vec3 getScale() const;
    void setScale(glm::vec3 scale);
    glm::vec3 getWorldPosition() const;
    void setWorldPosition(glm::vec3 pos);
    glm::quat getWorldRotation() const;
    void setWorldRotation(glm::quat rot);
    void translate(glm::vec3 delta);
    void rotate(float angleDeg, glm::vec3 axis);

    glm::vec3 getForward() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;

    Entity *getEntity() const { return getOwnerEntity(); }

    template <typename T, typename... Args>
    T *addComponent(Args &&...args)
    {
        return getOwnerEntity()->addComponent<T>(std::forward<Args>(args)...);
    }

    template <typename T>
    T *getComponent()
    {
        return getOwnerEntity()->getComponent<T>();
    }

    template <typename T>
    bool hasComponent() const
    {
        return getOwnerEntity()->hasComponent<T>();
    }

    static Entity *findEntityByName(const std::string &name);

    // Find the first VXActor-derived script of type T in the active scene.
    template <typename T>
    static T *findActor()
    {
        static_assert(std::is_base_of_v<VXActor, T>, "findActor<T>: T must derive from VXActor");
        auto *scene = VXGameState::get().getActiveScene();
        if (!scene)
            return nullptr;
        for (const auto &entity : scene->getEntities())
        {
            if (!entity)
                continue;
            for (auto *comp : entity->getComponents<ScriptComponent>())
            {
                if (auto *actor = dynamic_cast<T *>(comp->getScript()))
                    return actor;
            }
        }
        return nullptr;
    }

    // Find the first VXActor-derived script of type T on an entity with the given name.
    template <typename T>
    static T *findActor(const std::string &name)
    {
        static_assert(std::is_base_of_v<VXActor, T>, "findActor<T>: T must derive from VXActor");
        auto *entity = findEntityByName(name);
        if (!entity)
            return nullptr;
        for (auto *comp : entity->getComponents<ScriptComponent>())
        {
            if (auto *actor = dynamic_cast<T *>(comp->getScript()))
                return actor;
        }
        return nullptr;
    }

    // Spawn a new entity named `name` with a T script attached. onStart() is called immediately.
    template <typename T>
    static T *spawn(const std::string &name)
    {
        static_assert(std::is_base_of_v<VXActor, T>, "VXActor::spawn<T>: T must derive from VXActor");
        auto *entity = scripting::spawnEntity(name.c_str());
        if (!entity)
            return nullptr;
        auto *script = new T();
        auto *comp = entity->addComponent<ScriptComponent>(std::string(typeid(T).name()), script);
        if (comp)
            comp->onAttach(); // immediately calls script->onStart()
        return script;
    }

    // Destroy this actor's owner entity.
    void destroy();

    VXActor *getParentActor() const;

    void log(const std::string &msg) const;
    void logWarning(const std::string &msg) const;
    void logError(const std::string &msg) const;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_VX_ACTOR_HPP
