#ifndef ELIX_SDK_VX_ACTOR_HPP
#define ELIX_SDK_VX_ACTOR_HPP

#include "VelixSDK/VXObject.hpp"
#include "Engine/Input/InputManager.hpp"
#include "Engine/Input/Keyboard.hpp"

#include <glm/vec2.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(sdk)

// VXActor — base class for gameplay scripts that need input.
// Routed through InputManager, so edge detection, deltas, and cursor
// control all work correctly.
class VXActor : public VXObject
{
public:
    void onUpdate(float deltaTime) override;

    // --- Keyboard (held) ---
    bool isKeyDown(engine::KeyCode keyCode) const;
    bool isKeyDown(int keyCode) const;

    // --- Keyboard (edge) ---
    bool isKeyJustPressed(engine::KeyCode keyCode) const;
    bool isKeyJustPressed(int keyCode) const;
    bool isKeyJustReleased(engine::KeyCode keyCode) const;
    bool isKeyJustReleased(int keyCode) const;

    // Convenience: true while key is NOT held
    bool isKeyUp(engine::KeyCode keyCode) const;
    bool isKeyUp(int keyCode) const;

    // --- Mouse buttons ---
    bool isMouseButtonDown(int button) const;
    bool isMouseButtonDown(engine::MouseButton button) const;
    bool isMouseButtonJustPressed(int button) const;
    bool isMouseButtonJustPressed(engine::MouseButton button) const;
    bool isMouseButtonJustReleased(int button) const;
    bool isMouseButtonJustReleased(engine::MouseButton button) const;
    bool isMouseButtonUp(int button) const;

    // --- Mouse position & movement ---
    glm::vec2 getMousePosition() const;
    glm::vec2 getMouseDelta() const;
    float getScrollDelta() const;

    // --- Cursor control ---
    void setCursorLocked(bool locked);
    bool isCursorLocked() const;
    void setCursorVisible(bool visible);
    bool isCursorVisible() const;

protected:
    virtual void onInput(float deltaTime)
    {
        (void)deltaTime;
    }

    virtual void onActorUpdate(float deltaTime)
    {
        (void)deltaTime;
    }

private:
    static engine::InputManager &input();
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SDK_VX_ACTOR_HPP
