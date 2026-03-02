#ifndef ELIX_UI_TEXT_HPP
#define ELIX_UI_TEXT_HPP

#include "Core/Macros.hpp"
#include "Engine/UI/Font.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(ui)

/// Screen-space text game object rendered by UIRenderGraphPass.
/// Position is in normalised device coordinates: (-1,-1) = bottom-left, (1,1) = top-right.
class UIText
{
public:
    bool loadFont(const std::string &fontPath);

    void              setText(const std::string &text);
    const std::string &getText() const;

    void             setPosition(const glm::vec2 &ndcPos);
    const glm::vec2 &getPosition() const;

    void  setScale(float scale);
    float getScale() const;

    void  setRotation(float rotationDegrees);
    float getRotation() const;

    void             setColor(const glm::vec4 &color);
    const glm::vec4 &getColor() const;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    const Font *getFont() const;

private:
    std::string m_text;
    glm::vec2   m_position{-0.9f, 0.9f};
    float       m_scale{1.0f};
    float       m_rotation{0.0f};
    glm::vec4   m_color{1.0f};
    bool        m_enabled{true};

    Font m_font;
    bool m_fontLoaded{false};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_UI_TEXT_HPP
