#include "VelixSDK/VXActor.hpp"

#include "Engine/Scripting/VelixAPI.hpp"

#include <GLFW/glfw3.h>

ELIX_NESTED_NAMESPACE_BEGIN(sdk)

namespace
{
    GLFWwindow *resolveActiveGlfwWindow()
    {
        auto *window = engine::scripting::getActiveWindow();
        return window ? window->getRawHandler() : nullptr;
    }
} // namespace

void VXActor::onUpdate(float deltaTime)
{
    onInput(deltaTime);
    onActorUpdate(deltaTime);
}

bool VXActor::isKeyDown(engine::KeyCode keyCode) const
{
    auto *window = resolveActiveGlfwWindow();
    if (!window)
        return false;

    return glfwGetKey(window, static_cast<int>(keyCode)) == GLFW_PRESS;
}

bool VXActor::isKeyUp(engine::KeyCode keyCode) const
{
    auto *window = resolveActiveGlfwWindow();
    if (!window)
        return true;

    return glfwGetKey(window, static_cast<int>(keyCode)) == GLFW_RELEASE;
}

bool VXActor::isMouseButtonDown(int32_t button) const
{
    auto *window = resolveActiveGlfwWindow();
    if (!window)
        return false;

    return glfwGetMouseButton(window, button) == GLFW_PRESS;
}

bool VXActor::isMouseButtonUp(int32_t button) const
{
    auto *window = resolveActiveGlfwWindow();
    if (!window)
        return true;

    return glfwGetMouseButton(window, button) == GLFW_RELEASE;
}

glm::vec2 VXActor::getMousePosition() const
{
    auto *window = resolveActiveGlfwWindow();
    if (!window)
        return glm::vec2(0.0f);

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    return glm::vec2(static_cast<float>(x), static_cast<float>(y));
}

ELIX_NESTED_NAMESPACE_END