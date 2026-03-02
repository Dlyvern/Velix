#include "Engine/UI/UIText.hpp"
#include "Core/Logger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(ui)

bool UIText::loadFont(const std::string &fontPath)
{
    if (!m_font.load(fontPath))
    {
        VX_ENGINE_ERROR_STREAM("UIText: failed to load font: " << fontPath);
        return false;
    }
    m_fontLoaded = true;
    return true;
}

void UIText::setText(const std::string &text) { m_text = text; }
const std::string &UIText::getText() const     { return m_text; }

void UIText::setPosition(const glm::vec2 &ndcPos) { m_position = ndcPos; }
const glm::vec2 &UIText::getPosition() const      { return m_position; }

void  UIText::setScale(float scale) { m_scale = scale; }
float UIText::getScale()      const { return m_scale; }

void  UIText::setRotation(float rotationDegrees) { m_rotation = rotationDegrees; }
float UIText::getRotation() const { return m_rotation; }

void UIText::setColor(const glm::vec4 &color) { m_color = color; }
const glm::vec4 &UIText::getColor()     const { return m_color; }

void UIText::setEnabled(bool enabled) { m_enabled = enabled; }
bool UIText::isEnabled()        const { return m_enabled; }

const Font *UIText::getFont() const { return m_fontLoaded ? &m_font : nullptr; }

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
