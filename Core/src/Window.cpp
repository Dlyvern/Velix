#include "Core/Window.hpp"

#include <GLFW/glfw3.h>

#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(platform)

Window::Window(uint32_t width, uint32_t height, const std::string& title) : m_width(width), m_height(height), m_title(title)
{
    if(!glfwInit())
        throw std::runtime_error("Failed to initialize");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_window = glfwCreateWindow((int)width, (int)height, title.c_str(), nullptr, nullptr);

    if(!m_window)
        throw std::runtime_error("Failed to create GLFW window");
}

Window::~Window()
{
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

void Window::pollEvents()
{
    glfwPollEvents();
}

bool Window::isOpen() const
{
    return !glfwWindowShouldClose(m_window);
}

void Window::setTitle(const std::string& title)
{
    m_title = title;
    glfwSetWindowTitle(m_window, title.c_str());
}

GLFWwindow* Window::getRawHandler()
{
    return m_window;
}

std::shared_ptr<Window> Window::create(uint32_t width, uint32_t height, const std::string& title)
{
    return std::make_shared<Window>(width, height, title);
}

ELIX_NESTED_NAMESPACE_END