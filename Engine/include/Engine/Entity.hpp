#ifndef ELIX_ENTITY_HPP
#define ELIX_ENTITY_HPP

#include "Core/Macros.hpp"
#include "Engine/Components/ECS.hpp"

#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <unordered_set>
#include <string>

template <typename T>
struct IsMultiComponent
{
    static constexpr bool value = false;
};

template <>
struct IsMultiComponent<class AudioComponent>
{
    static constexpr bool value = true;
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Entity
{
public:
    using SharedPtr = std::shared_ptr<Entity>;

    Entity(const std::string &name);

    virtual void update(float deltaTime)
    {
        for (auto &component : m_components)
            component.second->update(deltaTime);
    }

    virtual void fixedUpdate(float fixedDelta) {}

    template <typename T, typename... Args>
    T *addComponent(Args &&...args)
    {
        static_assert(!std::is_abstract_v<T>, "Entity::addComponent() Cannot add abstract component!");
        static_assert(std::is_base_of_v<ECS, T>, "Entity::addComponent() T must derive from ECS class");

        const auto type = std::type_index(typeid(T));
        auto comp = std::make_shared<T>(std::forward<Args>(args)...);
        T *ptr = comp.get();
        comp->setOwner(this);
        // comp->onAttach();

        if constexpr (IsMultiComponent<T>::value)
            m_multiComponents[type].emplace_back(std::move(comp));
        else
            m_components[type] = std::move(comp);
        return ptr;
    }

    template <typename T>
    T *getComponent()
    {
        const auto it = m_components.find(std::type_index(typeid(T)));
        return it != m_components.end() ? static_cast<T *>(it->second.get()) : nullptr;
    }

    template <typename T>
    void removeComponent()
    {
        const auto type = std::type_index(typeid(T));

        if constexpr (IsMultiComponent<T>::value)
        {
            m_multiComponents.erase(type);
        }
        else
        {
            auto it = m_components.find(type);

            if (it == m_components.end())
                return;

            it->second->onDetach();
            m_components.erase(it);
        }
    }

    const std::unordered_map<std::type_index, std::shared_ptr<ECS>> &getSingleComponents() const
    {
        return m_components;
    }

    template <typename T>
    std::vector<T *> getComponents()
    {
        std::vector<T *> result;
        const auto type = std::type_index(typeid(T));

        if constexpr (IsMultiComponent<T>::value)
        {
            auto it = m_multiComponents.find(type);

            if (it != m_multiComponents.end())
                for (auto &comp : it->second)
                    result.push_back(static_cast<T *>(comp.get()));
        }
        else
        {
            auto it = m_components.find(type);

            if (it != m_components.end())
                result.push_back(static_cast<T *>(it->second.get()));
        }

        return result;
    }

    template <typename T>
    bool hasComponent() const
    {
        return m_components.contains(std::type_index(typeid(T)));
    }

    bool hasComponents() const
    {
        return !m_components.empty() && !m_multiComponents.empty();
    }

    void addTag(const std::string &tag);
    bool removeTag(const std::string &tag);
    bool hasTag(const std::string &tag) const;

    const std::string &getName() const;
    void setName(const std::string &name);

    uint64_t getId() const;
    void setId(uint64_t id);

    void setLayer(int layerID);
    int getLayer() const;

    virtual ~Entity();

private:
    std::unordered_map<std::type_index, std::shared_ptr<ECS>> m_components;
    std::unordered_map<std::type_index, std::vector<std::shared_ptr<ECS>>> m_multiComponents;

    SharedPtr m_parent{nullptr};
    std::vector<SharedPtr> m_children;

    int m_layer{0};
    std::unordered_set<std::string> m_tags;

    uint64_t m_id;
    std::string m_name;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ENTITY_HPP