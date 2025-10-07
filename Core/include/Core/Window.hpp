#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <cstdint>
#include <string>
#include <memory>
#include <functional>
#include <vector>

#include "Macros.hpp"

struct GLFWwindow;

ELIX_NESTED_NAMESPACE_BEGIN(platform)

class Window
{
public:
    using SharedPtr = std::shared_ptr<Window>;
    Window(uint32_t width, uint32_t height, const std::string& title);
    static SharedPtr create(uint32_t width, uint32_t height, const std::string& title);

    void pollEvents();

    bool isOpen() const;

    void setTitle(const std::string& title);

    virtual ~Window();

    void addResizeCallback(const std::function<void(Window*, int, int)>& function);

    GLFWwindow* getRawHandler();
private:
    static void onWindowResize(GLFWwindow *window, int width, int height);
    std::vector<std::function<void(Window*, int, int)>> m_resizeCallbacks;
    int m_width, m_height{0};
    std::string m_title;
    GLFWwindow* m_window{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //WINDOW_HPP