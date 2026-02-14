#include "Engine/Entity.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Entity::Entity(const std::string &name) : m_name(name)
{
}

Entity::Entity(Entity &other, const std::string &name, uint32_t id)
{
    this->m_components = other.m_components;
    this->m_multiComponents = other.m_multiComponents;
    this->m_name = name;
    this->m_id = id;
}

uint32_t Entity::getId() const
{
    return m_id;
}

void Entity::setId(uint32_t id)
{
    m_id = id;
}

void Entity::addTag(const std::string &tag)
{
    m_tags.insert(tag);
}

const std::string &Entity::getName() const
{
    return m_name;
}

void Entity::setName(const std::string &name)
{
    m_name = name;
}

bool Entity::removeTag(const std::string &tag)
{
    // auto it = std::find_if(m_tags.begin(), m_tags.end(), [&tag](const auto& t) {return tag == t; });

    // if(it != m_tags.end())
    //     m_tags.erase(it);

    // return it != m_tags.end();
    return false;
}

bool Entity::hasTag(const std::string &tag) const
{
    // auto it = std::find_if(m_tags.begin(), m_tags.end(), [&tag](const auto& t) {return tag == t; });

    // return it != m_tags.end();
    return false;
}

void Entity::setLayer(int layerID)
{
    m_layer = layerID;
}

int Entity::getLayer() const
{
    return m_layer;
}

Entity::~Entity()
{
    for (const auto &[id, component] : m_components)
        component->onDetach();

    for (const auto &[id, components] : m_multiComponents)
        for (const auto &component : components)
            component->onDetach();
}

ELIX_NESTED_NAMESPACE_END