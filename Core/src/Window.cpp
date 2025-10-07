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

    glfwSetWindowUserPointer(m_window, this);
    
    glfwSetFramebufferSizeCallback(m_window, &Window::onWindowResize);
}

void Window::addResizeCallback(const std::function<void(Window*, int, int)>& function)
{
    m_resizeCallbacks.push_back(function);
}

void Window::onWindowResize(GLFWwindow *window, int width, int height)
{
    auto platformWindow = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));

    if(!platformWindow)
        return;
    
    for(const auto& function : platformWindow->m_resizeCallbacks)
        function(platformWindow, width, height);
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

Window::SharedPtr Window::create(uint32_t width, uint32_t height, const std::string& title)
{
    return std::make_shared<Window>(width, height, title);
}

ELIX_NESTED_NAMESPACE_END