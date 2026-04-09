#include "Engine/Input/InputManager.hpp"

#include <GLFW/glfw3.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace
{
    // All GLFW key codes we care about polling each frame
    constexpr int k_keysToPoll[] = {
        // Letters
        65, 66, 67, 68, 69, 70, 71, 72, 73, 74,
        75, 76, 77, 78, 79, 80, 81, 82, 83, 84,
        85, 86, 87, 88, 89, 90,
        // Digits
        48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
        // Printable specials
        32,   // SPACE
        39,   // APOSTROPHE
        44,   // COMMA
        45,   // MINUS
        46,   // PERIOD
        47,   // SLASH
        // Control
        256,  // ESCAPE
        257,  // ENTER
        258,  // TAB
        259,  // BACKSPACE
        260,  // INSERT
        261,  // DELETE
        // Arrow keys
        262, 263, 264, 265,
        // Function keys F1-F12
        290, 291, 292, 293, 294, 295,
        296, 297, 298, 299, 300, 301,
        // Modifiers
        340, 341, 342, 344, 345, 346,
    };

    void scrollCallback(GLFWwindow * /*window*/, double /*xOffset*/, double yOffset)
    {
        InputManager::instance().onScrollEvent(static_cast<float>(yOffset));
    }
}

InputManager &InputManager::instance()
{
    static InputManager s_instance;
    return s_instance;
}

void InputManager::setWindow(GLFWwindow *window)
{
    m_window = window;
    m_firstUpdate = true;

    if (window)
        glfwSetScrollCallback(window, scrollCallback);
}

void InputManager::update()
{
    if (!m_window)
        return;

    // --- Keyboard ---
    m_previousKeys = m_currentKeys;
    m_currentKeys.clear();

    for (int key : k_keysToPoll)
    {
        if (glfwGetKey(m_window, key) == GLFW_PRESS)
            m_currentKeys.insert(key);
    }

    // --- Mouse buttons ---
    m_previousMouseButtons = m_currentMouseButtons;
    for (int i = 0; i < k_mouseButtonCount; ++i)
        m_currentMouseButtons[i] = (glfwGetMouseButton(m_window, i) == GLFW_PRESS);

    // --- Mouse position ---
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(m_window, &mx, &my);
    const glm::vec2 newPos(static_cast<float>(mx), static_cast<float>(my));

    if (m_firstUpdate)
    {
        m_mousePosition = newPos;
        m_firstUpdate = false;
    }

    m_mouseDelta = newPos - m_mousePosition;
    m_mousePosition = newPos;

    // --- Scroll: swap accumulator into readable delta ---
    m_scrollDelta = m_scrollAccum;
    m_scrollAccum = 0.0f;
}

// --- Keyboard queries ---

bool InputManager::isKeyDown(KeyCode key) const
{
    if (m_gameplayInputSuppressed)
        return false;

    return m_currentKeys.count(static_cast<int>(key)) > 0;
}

bool InputManager::isKeyJustPressed(KeyCode key) const
{
    if (m_gameplayInputSuppressed)
        return false;

    const int k = static_cast<int>(key);
    return m_currentKeys.count(k) > 0 && m_previousKeys.count(k) == 0;
}

bool InputManager::isKeyJustReleased(KeyCode key) const
{
    if (m_gameplayInputSuppressed)
        return false;

    const int k = static_cast<int>(key);
    return m_currentKeys.count(k) == 0 && m_previousKeys.count(k) > 0;
}

// --- Mouse button queries ---

bool InputManager::isMouseButtonDown(int button) const
{
    if (m_gameplayInputSuppressed)
        return false;

    if (button < 0 || button >= k_mouseButtonCount)
        return false;
    return m_currentMouseButtons[button];
}

bool InputManager::isMouseButtonDown(MouseButton button) const
{
    return isMouseButtonDown(static_cast<int>(button));
}

bool InputManager::isMouseButtonJustPressed(int button) const
{
    if (m_gameplayInputSuppressed)
        return false;

    if (button < 0 || button >= k_mouseButtonCount)
        return false;
    return m_currentMouseButtons[button] && !m_previousMouseButtons[button];
}

bool InputManager::isMouseButtonJustPressed(MouseButton button) const
{
    return isMouseButtonJustPressed(static_cast<int>(button));
}

bool InputManager::isMouseButtonJustReleased(int button) const
{
    if (m_gameplayInputSuppressed)
        return false;

    if (button < 0 || button >= k_mouseButtonCount)
        return false;
    return !m_currentMouseButtons[button] && m_previousMouseButtons[button];
}

bool InputManager::isMouseButtonJustReleased(MouseButton button) const
{
    return isMouseButtonJustReleased(static_cast<int>(button));
}

// --- Mouse position & movement ---

glm::vec2 InputManager::getMousePosition() const
{
    return m_mousePosition;
}

glm::vec2 InputManager::getMouseDelta() const
{
    if (m_gameplayInputSuppressed)
        return glm::vec2(0.0f);

    return m_mouseDelta;
}

float InputManager::getScrollDelta() const
{
    if (m_gameplayInputSuppressed)
        return 0.0f;

    return m_scrollDelta;
}

// --- Cursor control ---

void InputManager::setCursorLocked(bool locked)
{
    m_cursorLocked = locked;
    m_firstUpdate = true; // reset delta to avoid a position jump after mode change
    applyCursorMode();
}

bool InputManager::isCursorLocked() const
{
    return !m_gameplayInputSuppressed && m_cursorLocked;
}

void InputManager::setCursorVisible(bool visible)
{
    m_cursorVisible = visible;
    applyCursorMode();
}

bool InputManager::isCursorVisible() const
{
    return m_gameplayInputSuppressed || m_cursorVisible;
}

void InputManager::setGameplayInputSuppressed(bool suppressed)
{
    if (m_gameplayInputSuppressed == suppressed)
        return;

    m_gameplayInputSuppressed = suppressed;
    m_firstUpdate = true; // avoid a large mouse delta when recapturing gameplay input
    applyCursorMode();
}

bool InputManager::isGameplayInputSuppressed() const
{
    return m_gameplayInputSuppressed;
}

void InputManager::onScrollEvent(float yDelta)
{
    m_scrollAccum += yDelta;
}

void InputManager::applyCursorMode()
{
    if (!m_window)
        return;

    int mode;
    if (m_gameplayInputSuppressed)
        mode = GLFW_CURSOR_NORMAL;
    else if (m_cursorLocked)
        mode = GLFW_CURSOR_DISABLED;
    else if (!m_cursorVisible)
        mode = GLFW_CURSOR_HIDDEN;
    else
        mode = GLFW_CURSOR_NORMAL;

    glfwSetInputMode(m_window, GLFW_CURSOR, mode);
}

ELIX_NESTED_NAMESPACE_END
