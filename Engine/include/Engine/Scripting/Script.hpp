#ifndef ELIX_SCRIPT_HPP
#define ELIX_SCRIPT_HPP

#include "Core/Macros.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Script
{
public:
    virtual void onUpdate(float deltaTime) {}
    virtual void onStart() {}
    
    virtual ~Script() = default;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_SCRIPT_HPP