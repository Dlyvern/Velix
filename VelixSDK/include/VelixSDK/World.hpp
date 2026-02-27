#ifndef ELIX_SDK_WORLD_HPP
#define ELIX_SDK_WORLD_HPP

#include "Engine/Scripting/VelixAPI.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(sdk)

class World
{
public:
    explicit World(engine::Scene *scene = nullptr) : m_scene(scene)
    {
    }

    static World active()
    {
        return World(engine::scripting::getActiveScene());
    }

    void setScene(engine::Scene *scene)
    {
        m_scene = scene;
    }

    engine::Scene *getScene() const
    {
        return m_scene ? m_scene : engine::scripting::getActiveScene();
    }

    bool isValid() const
    {
        return getScene() != nullptr;
    }

    engine::Entity *spawn(const std::string &name = "Entity") const
    {
        return engine::scripting::spawnEntity(name.c_str(), m_scene);
    }

    bool remove(engine::Entity *entity) const
    {
        return engine::scripting::destroyEntity(entity, m_scene);
    }

    engine::Entity *findById(uint32_t id) const
    {
        return engine::scripting::findEntityById(id, m_scene);
    }

    std::size_t size() const
    {
        return static_cast<std::size_t>(engine::scripting::getEntitiesCount(m_scene));
    }

    bool empty() const
    {
        return size() == 0;
    }

    std::vector<engine::Entity *> entities() const
    {
        std::vector<engine::Entity *> result;
        const uint64_t count = engine::scripting::getEntitiesCount(m_scene);
        result.reserve(static_cast<std::size_t>(count));

        for (uint64_t index = 0; index < count; ++index)
        {
            auto *entity = engine::scripting::getEntityByIndex(index, m_scene);
            if (entity)
                result.push_back(entity);
        }

        return result;
    }

    std::size_t clear() const
    {
        auto allEntities = entities();
        std::size_t removedCount = 0;

        for (auto *entity : allEntities)
            if (remove(entity))
                ++removedCount;

        return removedCount;
    }

private:
    engine::Scene *m_scene{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SDK_WORLD_HPP
