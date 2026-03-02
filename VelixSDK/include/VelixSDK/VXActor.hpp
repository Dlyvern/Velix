#ifndef ELIX_SDK_VX_ACTOR_HPP
#define ELIX_SDK_VX_ACTOR_HPP

#include "VelixSDK/VXObject.hpp"
#include "Engine/Input/Keyboard.hpp"

#include <glm/vec2.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(sdk)

class VXActor : public VXObject
{
public:
    void onUpdate(float deltaTime) override;

    bool isKeyDown(engine::KeyCode keyCode) const;
    bool isKeyUp(engine::KeyCode keyCode) const;

    bool isMouseButtonDown(int32_t button) const;
    bool isMouseButtonUp(int32_t button) const;
    glm::vec2 getMousePosition() const;

protected:
    virtual void onInput(float deltaTime)
    {
        (void)deltaTime;
    }

    virtual void onActorUpdate(float deltaTime)
    {
        (void)deltaTime;
    }
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SDK_VX_ACTOR_HPP
