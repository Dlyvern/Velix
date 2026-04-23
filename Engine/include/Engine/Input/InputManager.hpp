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


    void update();


    void setWindow(GLFWwindow *window);




    bool isKeyDown(KeyCode key) const;


    bool isKeyJustPressed(KeyCode key) const;


    bool isKeyJustReleased(KeyCode key) const;




    bool isMouseButtonDown(int button) const;
    bool isMouseButtonDown(MouseButton button) const;

    bool isMouseButtonJustPressed(int button) const;
    bool isMouseButtonJustPressed(MouseButton button) const;

    bool isMouseButtonJustReleased(int button) const;
    bool isMouseButtonJustReleased(MouseButton button) const;




    glm::vec2 getMousePosition() const;


    glm::vec2 getMouseDelta() const;


    float getScrollDelta() const;




    void setCursorLocked(bool locked);
    bool isCursorLocked() const;


    void setCursorVisible(bool visible);
    bool isCursorVisible() const;


    void setGameplayInputSuppressed(bool suppressed);
    bool isGameplayInputSuppressed() const;


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

    float m_scrollAccum{0.0f};
    float m_scrollDelta{0.0f};

    bool m_firstUpdate{true};
    bool m_cursorLocked{false};
    bool m_cursorVisible{true};
    bool m_gameplayInputSuppressed{false};
};

ELIX_NESTED_NAMESPACE_END

#endif
