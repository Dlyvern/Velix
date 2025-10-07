#ifndef ELIX_ECS_HPP
#define ELIX_ECS_HPP

#include "Core/Macros.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ECS
{
public:
    virtual void update(float deltaTime) {}
    virtual void destroy() {}
    virtual ~ECS() = default;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_ECS_HPP