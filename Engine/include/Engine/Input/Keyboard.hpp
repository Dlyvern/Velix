#ifndef ELIX_KEYBOARD_HPP
#define ELIX_KEYBOARD_HPP

#include "Core/Window.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

enum class KeyCode : int16_t
{
    UNKNOWN = -1,
    A = 65,
    B = 66,
    C = 67,
    D = 68,
    E = 69,
    F = 70,
    G = 71,
    H = 72,
    I = 73,
    J = 74,
    K = 75,
    L = 76,
    M = 77,
    N = 78,
    O = 79,
    P = 80,
    Q = 81,
    R = 82,
    S = 83,
    T = 84,
    U = 85,
    V = 86,
    W = 87,
    X = 88,
    Y = 89,
    Z = 90,

    SPACE = 32,
    APOSTROPHE = 39,
    COMMA = 44,
    MINUS = 45,
    PERIOD = 46,  
    SLASH = 47,

    NUM_0 = 48,
    NUM_1 = 49,
    NUM_2 = 50,
    NUM_3 = 51,
    NUM_4 = 52,
    NUM_5 = 53,
    NUM_6 = 54,
    NUM_7 = 55,
    NUM_8 = 56,
    NUM_9 = 57,
};

class Keyboard
{
public:
    Keyboard(platform::Window::SharedPtr window);

    bool isButtonPressed(KeyCode keyCode);
    bool isButtonReleased(KeyCode keyCode);
private:
    std::weak_ptr<platform::Window> m_window;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_KEYBOARD_HPP