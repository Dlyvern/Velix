#ifndef ELIX_SDK_VX_OBJECT_HPP
#define ELIX_SDK_VX_OBJECT_HPP

#include "Engine/Scripting/Script.hpp"
#include "Engine/Scripting/VelixAPI.hpp"
#include "VelixSDK/World.hpp"

#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(sdk)

// Base class for all SDK objects
class VXObject : public engine::Script
{
public:
    engine::Scene *const getScene() const
    {
        return engine::scripting::getActiveScene();
    }

    engine::Entity *const getOuter() const
    {
        return this->getOwnerEntity();
    }

    World getWorld() const
    {
        return World(getScene());
    }

    std::string getName() const
    {
        return engine::scripting::getEntityName(getOuter());
    }
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SDK_VX_OBJECT_HPP
