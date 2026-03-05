#include "Core/Window.hpp"
#include "Core/Logger.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <stdexcept>
#include <iostream>
#include <algorithm>

#include <GLFW/glfw3.h>

ELIX_NESTED_NAMESPACE_BEGIN(platform)

Window::Window(uint32_t width, uint32_t height, const std::string &title, uint8_t windowFlags) : m_width(width), m_height(height), m_title(title)
{
    m_monitor = glfwGetPrimaryMonitor();

    if (!m_monitor)
        VX_WARNING("Failed to find monitor");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    if (windowFlags & EWINDOW_FLAGS_FULLSCREEN_WINDOWED)
    {
        const GLFWvidmode *mode = glfwGetVideoMode(m_monitor);
        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

        // Kinda bad...
        m_width = mode->width;
        m_height = mode->height;
    }

    // if(windowFlags & EWINDOW_FLAGS_NOT_RESIZABLE)
    //     glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    bool useMonitor = windowFlags & EWINDOW_FLAGS_FULLSCREEN;

    m_window = glfwCreateWindow(m_width, m_height, title.c_str(), useMonitor ? m_monitor : nullptr, nullptr);

    if (!m_window)
        throw std::runtime_error("Failed to create GLFW window");

    glfwSetWindowUserPointer(m_window, this);

    glfwSetFramebufferSizeCallback(m_window, &Window::onWindowResize);
    glfwSetWindowCloseCallback(m_window, &Window::onAboutToClose);
}

void Window::centerizedOnScreen()
{
    int sx = 0, sy = 0;
    int px = 0, py = 0;
    int mx = 0, my = 0;
    int monitor_count = 0;
    int best_area = 0;
    int final_x = 0, final_y = 0;

    glfwGetWindowSize(m_window, &sx, &sy);
    glfwGetWindowPos(m_window, &px, &py);

    // Iterate throug all monitors
    GLFWmonitor **m = glfwGetMonitors(&monitor_count);
    if (!m)
        return;

    for (int j = 0; j < monitor_count; ++j)
    {

        glfwGetMonitorPos(m[j], &mx, &my);
        const GLFWvidmode *mode = glfwGetVideoMode(m[j]);
        if (!mode)
            continue;

        int minX = (std::max)(mx, px);
        int minY = (std::max)(my, py);

        int maxX = (std::min)(mx + mode->width, px + sx);
        int maxY = (std::min)(my + mode->height, py + sy);

        int area = (std::max)(maxX - minX, 0) * (std::max)(maxY - minY, 0);

        if (area > best_area)
        {
            final_x = mx + (mode->width - sx) / 2;
            final_y = my + (mode->height - sy) / 2;

            best_area = area;
        }
    }

    if (best_area)
        glfwSetWindowPos(m_window, final_x, final_y);

    else
    {
        GLFWmonitor *primary = glfwGetPrimaryMonitor();
        if (primary)
        {
            const GLFWvidmode *desktop = glfwGetVideoMode(primary);

            if (desktop)
                glfwSetWindowPos(m_window, (desktop->width - sx) / 2, (desktop->height - sy) / 2);
            else
                return;
        }
        else
            return;
    }
}

void Window::setShowDecorations(bool enable)
{
    glfwSetWindowAttrib(m_window, GLFW_DECORATED, enable);
}

void Window::setPosition(int x, int y)
{
    glfwSetWindowPos(m_window, x, y);
}

void Window::onAboutToClose(GLFWwindow *window)
{
    auto platformWindow = reinterpret_cast<Window *>(glfwGetWindowUserPointer(window));

    if (!platformWindow)
        return;

    if (!platformWindow->m_onAboutToCloseCallback)
        return;

    glfwSetWindowShouldClose(window, platformWindow->m_onAboutToCloseCallback(platformWindow) ? GLFW_TRUE : GLFW_FALSE);
}

void Window::setOnWindowAboutToCloseCallback(const std::function<bool(Window *window)> &callback)
{
    m_onAboutToCloseCallback = callback;
}

void Window::getSize(int *width, int *height) const
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

void Window::getMaxMonitorResolution(int *width, int *height)
{
    m_monitor = glfwGetPrimaryMonitor();

    if (!m_monitor)
    {
        VX_CORE_ERROR_STREAM("Failed to get primary monitor" << std::endl);
        return;
    }

    int xpos, ypos;

    glfwGetMonitorWorkarea(m_monitor, &xpos, &ypos, width, height);
}

void Window::setFullscreen(bool enable)
{
    if (!m_window)
        return;

    if (m_isFullscreen == enable)
        return;

    m_monitor = glfwGetPrimaryMonitor();
    if (!m_monitor)
    {
        VX_WARNING("Failed to find primary monitor for fullscreen toggle");
        return;
    }

    const GLFWvidmode *mode = glfwGetVideoMode(m_monitor);
    if (!mode)
    {
        VX_WARNING("Failed to get video mode for fullscreen toggle");
        return;
    }

    if (enable)
    {
        // Save current windowed position + size
        glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
        glfwGetWindowSize(m_window, &m_windowedW, &m_windowedH);

        glfwSetWindowMonitor(
            m_window,
            m_monitor,
            0, 0,
            mode->width, mode->height,
            mode->refreshRate);

        m_width = static_cast<uint32_t>(mode->width);
        m_height = static_cast<uint32_t>(mode->height);
        m_isFullscreen = true;
    }
    else
    {
        glfwSetWindowMonitor(
            m_window,
            nullptr,
            m_windowedX, m_windowedY,
            m_windowedW, m_windowedH,
            0);

        m_width = static_cast<uint32_t>(m_windowedW);
        m_height = static_cast<uint32_t>(m_windowedH);
        m_isFullscreen = false;
    }
}

void Window::addResizeCallback(const std::function<void(Window *, int, int)> &function)
{
    m_resizeCallbacks.push_back(function);
}

void Window::onWindowResize(GLFWwindow *window, int width, int height)
{
    auto platformWindow = reinterpret_cast<Window *>(glfwGetWindowUserPointer(window));

    if (!platformWindow)
        return;

    for (const auto &function : platformWindow->m_resizeCallbacks)
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

void Window::setTitle(const std::string &title)
{
    m_title = title;
    glfwSetWindowTitle(m_window, title.c_str());
}

GLFWwindow *Window::getRawHandler()
{
    return m_window;
}

Window::SharedPtr Window::create(uint32_t width, uint32_t height, const std::string &title, uint8_t windowFlags)
{
    return std::make_shared<Window>(width, height, title, windowFlags);
}

ELIX_NESTED_NAMESPACE_END