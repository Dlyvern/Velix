#ifndef ELIX_VX_OBJECT_HPP
#define ELIX_VX_OBJECT_HPP

#include "Core/Macros.hpp"
#include "Engine/SDK/VXGameState.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Scene;

// Base class for all VelixSDK game objects.
// Provides static shortcuts to core engine services.
class VXObject
{
public:
    static VXGameState &getGameState() { return VXGameState::get(); }
    static Scene *getActiveScene() { return VXGameState::get().getActiveScene(); }
    static float getDeltaTime() { return VXGameState::get().getDeltaTime(); }

    virtual ~VXObject() = default;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_VX_OBJECT_HPP
