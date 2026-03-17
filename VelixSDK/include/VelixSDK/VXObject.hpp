#ifndef ELIX_SDK_VX_OBJECT_HPP
#define ELIX_SDK_VX_OBJECT_HPP

#include "Engine/Scripting/Script.hpp"
#include "Engine/Scripting/VelixAPI.hpp"
#include "Engine/Entity.hpp"
#include "Engine/Components/ECS.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Time.hpp"
#include "VelixSDK/World.hpp"

#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(sdk)

class VXObject : public engine::Script
{
public:
    using EntityRef = engine::Script::EntityRef;

    engine::Scene *const getScene() const
    {
        return engine::scripting::getActiveScene();
    }

    engine::Entity *const getOuter() const
    {
        return this->getOwnerEntity();
    }

    World getWorld() const
    {
        return World(getScene());
    }

    std::string getName() const
    {
        return engine::scripting::getEntityName(getOuter());
    }

    template <typename T>
    T *getComponent() const
    {
        auto *entity = getOuter();
        return entity ? entity->getComponent<T>() : nullptr;
    }

    template <typename T>
    std::vector<T *> getComponents() const
    {
        auto *entity = getOuter();
        return entity ? entity->getComponents<T>() : std::vector<T *>{};
    }

    engine::ECS *getComponentByName(const std::string &componentTypeName) const
    {
        return engine::scripting::getEntitySingleComponent(getOuter(), componentTypeName.c_str());
    }

    std::vector<engine::ECS *> getComponentsByName(const std::string &componentTypeName) const
    {
        std::vector<engine::ECS *> result;
        const uint64_t count = engine::scripting::getEntityComponentsCount(getOuter(), componentTypeName.c_str());
        result.reserve(static_cast<size_t>(count));

        for (uint64_t index = 0; index < count; ++index)
        {
            auto *component = engine::scripting::getEntityComponentByIndex(getOuter(), componentTypeName.c_str(), index);
            if (component)
                result.push_back(component);
        }

        return result;
    }

    bool hasComponentByName(const std::string &componentTypeName) const
    {
        return engine::scripting::entityHasComponent(getOuter(), componentTypeName.c_str());
    }

    engine::RenderQualitySettings &getRenderSettings() const
    {
        return *engine::scripting::getRenderQualitySettings();
    }

    engine::Entity *resolveEntity(const EntityRef &entityRef) const
    {
        if (!entityRef.isValid())
            return nullptr;

        return getWorld().findById(entityRef.id);
    }

    EntityRef makeEntityRef(const engine::Entity *entity) const
    {
        return entity ? EntityRef(entity->getId()) : EntityRef{};
    }

    engine::Entity *findEntity(const std::string &name) const
    {
        return engine::scripting::findEntityByName(name.c_str(), getScene());
    }

    void dontDestroyOnLoad()
    {
        engine::scripting::setDontDestroyOnLoad(getOuter());
    }

    void destroyOnLoad()
    {
        engine::scripting::clearDontDestroyOnLoad(getOuter());
    }

    // --- Transform helpers (operate on this entity) ---

    glm::vec3 getWorldPosition() const
    {
        return engine::scripting::getWorldPosition(getOuter());
    }

    void setWorldPosition(const glm::vec3 &position)
    {
        engine::scripting::setWorldPosition(getOuter(), position);
    }

    glm::quat getWorldRotation() const
    {
        return engine::scripting::getWorldRotation(getOuter());
    }

    void setWorldRotation(const glm::quat &rotation)
    {
        engine::scripting::setWorldRotation(getOuter(), rotation);
    }

    glm::vec3 getForwardVector() const
    {
        return engine::scripting::getForwardVector(getOuter());
    }

    glm::vec3 getRightVector() const
    {
        return engine::scripting::getRightVector(getOuter());
    }

    glm::vec3 getUpVector() const
    {
        return engine::scripting::getUpVector(getOuter());
    }

    // --- Physics helpers (operate on this entity) ---

    void addImpulse(const glm::vec3 &impulse)
    {
        engine::scripting::addImpulse(getOuter(), impulse);
    }

    void addForce(const glm::vec3 &force)
    {
        engine::scripting::addForce(getOuter(), force);
    }

    // --- Physics queries ---

    bool raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                 engine::PhysicsRaycastHit *outHit = nullptr) const
    {
        return engine::scripting::raycast(origin, direction, maxDistance, outHit, getScene());
    }

    static float deltaTime() { return engine::Time::instance().deltaTime(); }
    static float totalTime() { return engine::Time::instance().totalTime(); }
    static float scaledDeltaTime() { return engine::Time::instance().scaledDeltaTime(); }
    static uint64_t frameCount() { return engine::Time::instance().frameCount(); }
    static float timeScale() { return engine::Time::instance().timeScale(); }
    static void setTimeScale(float scale) { engine::Time::instance().setTimeScale(scale); }
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SDK_VX_OBJECT_HPP
