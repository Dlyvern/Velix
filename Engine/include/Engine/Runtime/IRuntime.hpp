#ifndef ELIX_IRUNTIME_HPP
#define ELIX_IRUNTIME_HPP

#include "Core/Macros.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class IRuntime
{
public:
    virtual ~IRuntime() = default;

    virtual bool init() = 0;
    virtual void tick(float deltaTime) = 0;
    virtual void shutdown() = 0;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_IRUNTIME_HPP