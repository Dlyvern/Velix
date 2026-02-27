#ifndef ELIX_SCRIPT_HPP
#define ELIX_SCRIPT_HPP

#include "Core/Macros.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Entity;

class Script
{
public:
    virtual void onUpdate(float deltaTime) {}
    virtual void onStart() {}
    virtual void onStop() {}

    Entity *getOwnerEntity() const
    {
        return m_ownerEntity;
    }

    void setOwnerEntity(Entity *entity)
    {
        m_ownerEntity = entity;
    }

    virtual ~Script()
    {
    }

private:
    Entity *m_ownerEntity{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SCRIPT_HPP
