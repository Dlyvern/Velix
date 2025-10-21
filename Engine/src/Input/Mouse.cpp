#include "Engine/Input/Mouse.hpp"

#include <GLFW/glfw3.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Mouse::Mouse(platform::Window::SharedPtr window) : m_window(window)
{

}

bool Mouse::isRightButtonReleased()
{
    return glfwGetMouseButton(m_window.lock()->getRawHandler(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_RELEASE;
}

bool Mouse::isRighButtontPressed()
{
    return glfwGetMouseButton(m_window.lock()->getRawHandler(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
}

ELIX_NESTED_NAMESPACE_END