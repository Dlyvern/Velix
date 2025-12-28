#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <cstdint>
#include <string>
#include <memory>
#include <functional>
#include <vector>

#include "Core/Macros.hpp"

// #include <GLFW/glfw3.h>

struct GLFWwindow;
struct GLFWmonitor;

ELIX_NESTED_NAMESPACE_BEGIN(platform)

class Window
{
    // DECLARE_VK_SMART_PTRS(Window, GLFWWindow)
public:
    using SharedPtr = std::shared_ptr<Window>;

    enum WindowFlags : uint8_t
    {
        EWINDOW_FLAGS_DEFAULT = 0,
        EWINDOW_FLAGS_FULLSCREEN = 1 << 0,
        EWINDOW_FLAGS_FULLSCREEN_WINDOWED = 1 << 1,
        EWINDOW_FLAGS_NOT_RESIZABLE = 1 << 2,
        EWINDOW_FLAGS_HIDDEN = 1 << 3,
        EWINDOW_FLAGS_MAXIMIZED = 1 << 4
    };

    Window(uint32_t width, uint32_t height, const std::string& title, uint8_t windowFlags = EWINDOW_FLAGS_DEFAULT);
    static SharedPtr create(uint32_t width, uint32_t height, const std::string& title, uint8_t windowFlags = EWINDOW_FLAGS_DEFAULT);

    void pollEvents();
    void iconify();
    void close();

    bool isOpen() const;

    void addResizeCallback(const std::function<void(Window*, int, int)>& function);

    void setTitle(const std::string& title);
    void setPosition(int x, int y);
    void setSize(int width, int height);
    /*
        will be called on about to close callback. If returns false -> window will not close, true -> window will be closed
    */
    void setOnWindowAboutToCloseCallback(const std::function<bool(Window* window)>& callback);

    void getSize(int* width, int* height);
    void getMaxMonitorResolution(int* width, int* height);
    GLFWwindow* getRawHandler();

    virtual ~Window();
private:
    static void onAboutToClose(GLFWwindow* window);
    static void onWindowResize(GLFWwindow *window, int width, int height);
    std::vector<std::function<void(Window*, int, int)>> m_resizeCallbacks;
    int m_width, m_height{0};
    std::string m_title;
    GLFWwindow* m_window{nullptr};
    GLFWmonitor* m_monitor{nullptr};
    std::function<bool(Window* window)> m_onAboutToCloseCallback{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //WINDOW_HPP