#include "Engine/Entity.hpp"

#include <algorithm>

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
    this->m_enabled = other.m_enabled;
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

void Entity::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool Entity::isEnabled() const
{
    return m_enabled;
}

bool Entity::setParent(Entity *parent)
{
    if (parent == this)
        return false;

    if (parent && parent->isDescendantOf(this))
        return false;

    if (m_parent == parent)
        return true;

    if (m_parent)
    {
        auto &siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    }

    m_parent = parent;

    if (m_parent)
    {
        auto &newSiblings = m_parent->m_children;
        if (std::find(newSiblings.begin(), newSiblings.end(), this) == newSiblings.end())
            newSiblings.push_back(this);
    }

    return true;
}

void Entity::clearParent()
{
    setParent(nullptr);
}

Entity *Entity::getParent() const
{
    return m_parent;
}

const std::vector<Entity *> &Entity::getChildren() const
{
    return m_children;
}

bool Entity::isDescendantOf(const Entity *possibleAncestor) const
{
    if (!possibleAncestor)
        return false;

    const Entity *current = m_parent;
    while (current)
    {
        if (current == possibleAncestor)
            return true;

        current = current->m_parent;
    }

    return false;
}

Entity::~Entity()
{
    clearParent();

    for (auto *child : m_children)
        if (child)
            child->m_parent = nullptr;

    m_children.clear();

    for (const auto &[id, component] : m_components)
        component->onDetach();

    for (const auto &[id, components] : m_multiComponents)
        for (const auto &component : components)
            component->onDetach();
}

ELIX_NESTED_NAMESPACE_END
