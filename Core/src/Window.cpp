#include "Core/Window.hpp"

#include <GLFW/glfw3.h>

#include <stdexcept>

#include <iostream>

#include "Core/Logger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(platform)

Window::Window(uint32_t width, uint32_t height, const std::string& title, uint8_t windowFlags) : m_width(width), m_height(height), m_title(title)
{
    m_monitor = glfwGetPrimaryMonitor();

    if(!m_monitor)
        VX_WARNING("Failed to find monitor");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    if(windowFlags & EWINDOW_FLAGS_FULLSCREEN_WINDOWED)
    {
        const GLFWvidmode* mode = glfwGetVideoMode(m_monitor); 
        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

        //Kinda bad...
        m_width = mode->width;
        m_height = mode->height;
    }

    // if(windowFlags & EWINDOW_FLAGS_NOT_RESIZABLE)
    //     glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    bool useMonitor = windowFlags & EWINDOW_FLAGS_FULLSCREEN || windowFlags & EWINDOW_FLAGS_FULLSCREEN_WINDOWED;

    m_window = glfwCreateWindow(m_width, m_height, title.c_str(), useMonitor ? m_monitor : nullptr, nullptr);

    if(!m_window)
        throw std::runtime_error("Failed to create GLFW window");

    glfwSetWindowUserPointer(m_window, this);
    
    glfwSetFramebufferSizeCallback(m_window, &Window::onWindowResize);
    glfwSetWindowCloseCallback(m_window, &Window::onAboutToClose);
}

void Window::setPosition(int x, int y)
{
    glfwSetWindowPos(m_window, x, y);
}

void Window::onAboutToClose(GLFWwindow* window)
{
    auto platformWindow = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));

    if(!platformWindow)
        return;
    
    if(!platformWindow->m_onAboutToCloseCallback)
        return;

    glfwSetWindowShouldClose(window, platformWindow->m_onAboutToCloseCallback(platformWindow) ? GLFW_TRUE : GLFW_FALSE);
}

void Window::setOnWindowAboutToCloseCallback(const std::function<bool(Window* window)>& callback)
{
    m_onAboutToCloseCallback = callback;
}

void Window::getSize(int* width, int* height)
{
    glfwGetWindowSize(m_window, width, height);
}

void Window::close()
{
    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void Window::iconify()
{
    glfwIconifyWindow(m_window);
}

void Window::setSize(int width, int height)
{
    glfwSetWindowSize(m_window, width, height);
}

void Window::getMaxMonitorResolution(int* width, int* height)
{
    m_monitor = glfwGetPrimaryMonitor();

    if (!m_monitor) 
    {
        std::cerr << "Failed to get primary monitor" << std::endl;
        return;
    }

    int xpos, ypos;

    glfwGetMonitorWorkarea(m_monitor, &xpos, &ypos, width, height);
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

Window::SharedPtr Window::create(uint32_t width, uint32_t height, const std::string& title, uint8_t windowFlags)
{
    return std::make_shared<Window>(width, height, title, windowFlags);
}

ELIX_NESTED_NAMESPACE_END