#ifndef ELIX_ECS_HPP
#define ELIX_ECS_HPP

#include "Core/Macros.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ECS
{
public:
    virtual void update(float deltaTime) {}
    virtual void onAttach() {}
    virtual void onDetach() {}
    virtual ~ECS() = default;

    void setOwner(void *owner)
    {
        m_owner = owner;
        onOwnerAttached();
    }

    template <typename T>
    T *const getOwner() const
    {
        return static_cast<T *>(m_owner);
    }

    void *const getOwner() const
    {
        return m_owner;
    }

protected:
    virtual void onOwnerAttached() {}

private:
    void *m_owner{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ECS_HPP