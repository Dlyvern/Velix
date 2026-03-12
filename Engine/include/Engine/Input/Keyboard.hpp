#ifndef ELIX_KEYBOARD_HPP
#define ELIX_KEYBOARD_HPP

#include "Core/Window.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

enum class KeyCode : int16_t
{
    UNKNOWN = -1,

    // Printable
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

    // Control
    ESCAPE      = 256,
    ENTER       = 257,
    TAB         = 258,
    BACKSPACE   = 259,
    INSERT      = 260,
    DEL         = 261,

    // Arrow keys
    RIGHT = 262,
    LEFT  = 263,
    DOWN  = 264,
    UP    = 265,

    // Function keys
    F1  = 290,
    F2  = 291,
    F3  = 292,
    F4  = 293,
    F5  = 294,
    F6  = 295,
    F7  = 296,
    F8  = 297,
    F9  = 298,
    F10 = 299,
    F11 = 300,
    F12 = 301,

    // Modifiers
    LEFT_SHIFT    = 340,
    LEFT_CONTROL  = 341,
    LEFT_ALT      = 342,
    RIGHT_SHIFT   = 344,
    RIGHT_CONTROL = 345,
    RIGHT_ALT     = 346,
};

enum class MouseButton : int
{
    Left   = 0,
    Right  = 1,
    Middle = 2,
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