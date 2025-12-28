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

    void setOwner(void* owner) 
    {
        m_owner = owner;
    }

    template<typename T>
    void* const getOwner() const
    {
        return static_cast<T*>(m_owner);
    }

    void* const getOwner() const
    {
        return m_owner;
    }
private:
    void* m_owner{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_ECS_HPP