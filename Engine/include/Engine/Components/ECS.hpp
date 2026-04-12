#ifndef ELIX_ECS_HPP
#define ELIX_ECS_HPP

#include "Core/Macros.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ECS
{
public:
    virtual void update(float deltaTime) {}
    virtual void postPhysicsUpdate(float deltaTime) {}
    virtual void onAttach() {}
    virtual void onDetach() {}
    virtual ~ECS() = default;

    // Returns true if this component drives skeleton bone matrices directly
    // (e.g. MotionMatchingComponent). Used by the renderer to choose
    // getFinalMatrices() over getBindPoses() when no AnimatorComponent is active.
    virtual bool isSkeletonDriver() const { return false; }

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
