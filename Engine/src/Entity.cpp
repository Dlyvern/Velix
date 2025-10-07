#include "Engine/Entity.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Entity::Entity(const std::string& name) : m_name(name)
{
    
}

void Entity::addTag(const std::string& tag)
{
    m_tags.insert(tag);
}

const std::string& Entity::getName() const
{
    return m_name;
}

void Entity::setName(const std::string& name)
{
    m_name = name;
}

bool Entity::removeTag(const std::string& tag)
{
    auto it = std::find_if(m_tags.begin(), m_tags.end(), [&tag](const auto& t) {return tag == t; });

    if(it != m_tags.end())
        m_tags.erase(it);
    
    return it != m_tags.end();
}

bool Entity::hasTag(const std::string& tag) const
{
    auto it = std::find_if(m_tags.begin(), m_tags.end(), [&tag](const auto& t) {return tag == t; });

    return it != m_tags.end();
}

void Entity::setLayer(int layerID)
{
    m_layer = layerID;
}

int Entity::getLayer() const
{
    return m_layer;
}

ELIX_NESTED_NAMESPACE_END