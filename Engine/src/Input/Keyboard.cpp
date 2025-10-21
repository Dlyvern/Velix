#include "Engine/Input/Keyboard.hpp"

#include <GLFW/glfw3.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Keyboard::Keyboard(platform::Window::SharedPtr window) : m_window(window)
{
    
}

bool Keyboard::isButtonPressed(KeyCode keyCode)
{
    return glfwGetKey(m_window.lock()->getRawHandler(), static_cast<int>(keyCode)) == GLFW_PRESS;
}

bool Keyboard::isButtonReleased(KeyCode keyCode)
{
    return glfwGetKey(m_window.lock()->getRawHandler(), static_cast<int>(keyCode)) == GLFW_RELEASE;
}

ELIX_NESTED_NAMESPACE_END