#ifndef ELIX_UI_BUTTON_HPP
#define ELIX_UI_BUTTON_HPP

#include "Core/Macros.hpp"
#include "Engine/UI/Font.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <functional>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(ui)

/// Screen-space button game object with an optional text label.
/// Position/size are in NDC space: (-1,-1) = bottom-left, (1,1) = top-right.
class UIButton
{
public:
    bool loadFont(const std::string &fontPath);

    void             setPosition(const glm::vec2 &ndcPos);
    const glm::vec2 &getPosition() const;

    void             setSize(const glm::vec2 &ndcSize);
    const glm::vec2 &getSize() const;

    void             setBackgroundColor(const glm::vec4 &color);
    const glm::vec4 &getBackgroundColor() const;

    void             setHoverColor(const glm::vec4 &color);
    const glm::vec4 &getHoverColor() const;

    void             setBorderColor(const glm::vec4 &color);
    const glm::vec4 &getBorderColor() const;

    void  setBorderWidth(float width);
    float getBorderWidth() const;

    void              setLabel(const std::string &text);
    const std::string &getLabel() const;

    void             setLabelColor(const glm::vec4 &color);
    const glm::vec4 &getLabelColor() const;

    void  setLabelScale(float scale);
    float getLabelScale() const;

    void  setRotation(float rotationDegrees);
    float getRotation() const;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    bool isHovered() const;
    void setHovered(bool hovered);

    void setOnClick(std::function<void()> callback);
    void triggerClick();

    const Font *getFont() const;

private:
    glm::vec2 m_position{-0.1f, -0.1f};
    glm::vec2 m_size{0.2f, 0.1f};

    glm::vec4 m_backgroundColor{0.2f, 0.2f, 0.2f, 0.85f};
    glm::vec4 m_hoverColor{0.3f, 0.3f, 0.4f, 0.9f};
    glm::vec4 m_borderColor{0.5f, 0.5f, 0.6f, 1.0f};
    float     m_borderWidth{0.025f};

    std::string m_label;
    glm::vec4   m_labelColor{1.0f};
    float       m_labelScale{1.0f};
    float       m_rotation{0.0f};

    bool m_enabled{true};
    bool m_hovered{false};

    Font m_font;
    bool m_fontLoaded{false};

    std::function<void()> m_onClick{nullptr};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_UI_BUTTON_HPP
