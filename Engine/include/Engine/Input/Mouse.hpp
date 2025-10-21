#ifndef ELIX_MOUSE_HPP
#define ELIX_MOUSE_HPP

#include "Core/Macros.hpp"
#include "Core/Window.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Mouse
{
public:
    Mouse(platform::Window::SharedPtr window);

    bool isRighButtontPressed();
    bool isRightButtonReleased();

private:
    std::weak_ptr<platform::Window> m_window;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_MOUSE_HPP