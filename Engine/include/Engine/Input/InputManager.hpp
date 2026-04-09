#ifndef ELIX_INPUT_MANAGER_HPP
#define ELIX_INPUT_MANAGER_HPP

#include "Core/Macros.hpp"
#include "Engine/Input/Keyboard.hpp"

#include <glm/vec2.hpp>

#include <array>
#include <unordered_set>

struct GLFWwindow;

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class InputManager
{
public:
    static InputManager &instance();

    // Called once per frame after glfwPollEvents(), before scripts tick
    void update();

    // Sets the window to poll input from. Call when the active window changes.
    void setWindow(GLFWwindow *window);

    // --- Keyboard ---

    // True every frame the key is held down
    bool isKeyDown(KeyCode key) const;

    // True only on the frame the key was first pressed
    bool isKeyJustPressed(KeyCode key) const;

    // True only on the frame the key was released
    bool isKeyJustReleased(KeyCode key) const;

    // --- Mouse buttons ---

    // button: 0=Left, 1=Right, 2=Middle  (or use MouseButton enum cast to int)
    bool isMouseButtonDown(int button) const;
    bool isMouseButtonDown(MouseButton button) const;

    bool isMouseButtonJustPressed(int button) const;
    bool isMouseButtonJustPressed(MouseButton button) const;

    bool isMouseButtonJustReleased(int button) const;
    bool isMouseButtonJustReleased(MouseButton button) const;

    // --- Mouse position & movement ---

    // Cursor position in screen pixels (top-left origin)
    glm::vec2 getMousePosition() const;

    // Movement delta since last frame (0 on first frame)
    glm::vec2 getMouseDelta() const;

    // Accumulated scroll wheel delta this frame (positive = scroll up)
    float getScrollDelta() const;

    // --- Cursor control ---

    // Lock & hide cursor (disables OS acceleration — use for FPS look)
    void setCursorLocked(bool locked);
    bool isCursorLocked() const;

    // Show/hide cursor without locking
    void setCursorVisible(bool visible);
    bool isCursorVisible() const;

    // Editor/dev override: temporarily suppress gameplay input and force a free cursor.
    void setGameplayInputSuppressed(bool suppressed);
    bool isGameplayInputSuppressed() const;

    // Internal: called by the GLFW scroll callback
    void onScrollEvent(float yDelta);

private:
    InputManager() = default;

    void applyCursorMode();

    GLFWwindow *m_window{nullptr};

    std::unordered_set<int> m_currentKeys;
    std::unordered_set<int> m_previousKeys;

    static constexpr int k_mouseButtonCount = 8;
    std::array<bool, k_mouseButtonCount> m_currentMouseButtons{};
    std::array<bool, k_mouseButtonCount> m_previousMouseButtons{};

    glm::vec2 m_mousePosition{0.0f, 0.0f};
    glm::vec2 m_mouseDelta{0.0f, 0.0f};

    float m_scrollAccum{0.0f};  // written by scroll callback
    float m_scrollDelta{0.0f};  // readable this frame, swapped in update()

    bool m_firstUpdate{true};
    bool m_cursorLocked{false};
    bool m_cursorVisible{true};
    bool m_gameplayInputSuppressed{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_INPUT_MANAGER_HPP
