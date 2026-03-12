#include "VelixSDK/VXActor.hpp"

#include "Engine/Input/InputManager.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(sdk)

engine::InputManager &VXActor::input()
{
    return engine::InputManager::instance();
}

void VXActor::onUpdate(float deltaTime)
{
    onInput(deltaTime);
    onActorUpdate(deltaTime);
}

// --- Keyboard (held) ---

bool VXActor::isKeyDown(engine::KeyCode keyCode) const
{
    return input().isKeyDown(keyCode);
}

bool VXActor::isKeyDown(int keyCode) const
{
    return input().isKeyDown(static_cast<engine::KeyCode>(keyCode));
}

// --- Keyboard (edge) ---

bool VXActor::isKeyJustPressed(engine::KeyCode keyCode) const
{
    return input().isKeyJustPressed(keyCode);
}

bool VXActor::isKeyJustPressed(int keyCode) const
{
    return input().isKeyJustPressed(static_cast<engine::KeyCode>(keyCode));
}

bool VXActor::isKeyJustReleased(engine::KeyCode keyCode) const
{
    return input().isKeyJustReleased(keyCode);
}

bool VXActor::isKeyJustReleased(int keyCode) const
{
    return input().isKeyJustReleased(static_cast<engine::KeyCode>(keyCode));
}

bool VXActor::isKeyUp(engine::KeyCode keyCode) const
{
    return !input().isKeyDown(keyCode);
}

bool VXActor::isKeyUp(int keyCode) const
{
    return !input().isKeyDown(static_cast<engine::KeyCode>(keyCode));
}

// --- Mouse buttons ---

bool VXActor::isMouseButtonDown(int button) const
{
    return input().isMouseButtonDown(button);
}

bool VXActor::isMouseButtonDown(engine::MouseButton button) const
{
    return input().isMouseButtonDown(button);
}

bool VXActor::isMouseButtonJustPressed(int button) const
{
    return input().isMouseButtonJustPressed(button);
}

bool VXActor::isMouseButtonJustPressed(engine::MouseButton button) const
{
    return input().isMouseButtonJustPressed(button);
}

bool VXActor::isMouseButtonJustReleased(int button) const
{
    return input().isMouseButtonJustReleased(button);
}

bool VXActor::isMouseButtonJustReleased(engine::MouseButton button) const
{
    return input().isMouseButtonJustReleased(button);
}

bool VXActor::isMouseButtonUp(int button) const
{
    return !input().isMouseButtonDown(button);
}

// --- Mouse position & movement ---

glm::vec2 VXActor::getMousePosition() const
{
    return input().getMousePosition();
}

glm::vec2 VXActor::getMouseDelta() const
{
    return input().getMouseDelta();
}

float VXActor::getScrollDelta() const
{
    return input().getScrollDelta();
}

// --- Cursor control ---

void VXActor::setCursorLocked(bool locked)
{
    input().setCursorLocked(locked);
}

bool VXActor::isCursorLocked() const
{
    return input().isCursorLocked();
}

void VXActor::setCursorVisible(bool visible)
{
    input().setCursorVisible(visible);
}

bool VXActor::isCursorVisible() const
{
    return input().isCursorVisible();
}

ELIX_NESTED_NAMESPACE_END
