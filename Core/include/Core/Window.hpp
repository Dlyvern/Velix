#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <cstdint>
#include <string>
#include <memory>

#include "Macros.hpp"

struct GLFWwindow;

ELIX_NESTED_NAMESPACE_BEGIN(platform)

class Window
{
public:
    Window(uint32_t width, uint32_t height, const std::string& title);

    void pollEvents();

    bool isOpen() const;

    void setTitle(const std::string& title);

    virtual ~Window();

    static std::shared_ptr<Window> create(uint32_t width, uint32_t height, const std::string& title);

    GLFWwindow* getRawHandler();
private:
    int m_width, m_height{0};
    std::string m_title;
    GLFWwindow* m_window{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //WINDOW_HPP