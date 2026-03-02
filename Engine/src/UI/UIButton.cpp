#include "Engine/UI/UIButton.hpp"
#include "Core/Logger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(ui)

bool UIButton::loadFont(const std::string &fontPath)
{
    if (!m_font.load(fontPath))
    {
        VX_ENGINE_ERROR_STREAM("UIButton: failed to load font: " << fontPath);
        return false;
    }
    m_fontLoaded = true;
    return true;
}

void UIButton::setPosition(const glm::vec2 &ndcPos) { m_position = ndcPos; }
const glm::vec2 &UIButton::getPosition() const       { return m_position; }

void UIButton::setSize(const glm::vec2 &ndcSize) { m_size = ndcSize; }
const glm::vec2 &UIButton::getSize() const        { return m_size; }

void UIButton::setBackgroundColor(const glm::vec4 &color) { m_backgroundColor = color; }
const glm::vec4 &UIButton::getBackgroundColor() const     { return m_backgroundColor; }

void UIButton::setHoverColor(const glm::vec4 &color) { m_hoverColor = color; }
const glm::vec4 &UIButton::getHoverColor() const     { return m_hoverColor; }

void UIButton::setBorderColor(const glm::vec4 &color) { m_borderColor = color; }
const glm::vec4 &UIButton::getBorderColor() const     { return m_borderColor; }

void  UIButton::setBorderWidth(float width) { m_borderWidth = width; }
float UIButton::getBorderWidth()      const { return m_borderWidth; }

void UIButton::setLabel(const std::string &text) { m_label = text; }
const std::string &UIButton::getLabel() const    { return m_label; }

void UIButton::setLabelColor(const glm::vec4 &color) { m_labelColor = color; }
const glm::vec4 &UIButton::getLabelColor() const     { return m_labelColor; }

void  UIButton::setLabelScale(float scale) { m_labelScale = scale; }
float UIButton::getLabelScale()      const { return m_labelScale; }

void  UIButton::setRotation(float rotationDegrees) { m_rotation = rotationDegrees; }
float UIButton::getRotation() const { return m_rotation; }

void UIButton::setEnabled(bool enabled) { m_enabled = enabled; }
bool UIButton::isEnabled()        const { return m_enabled; }

bool UIButton::isHovered() const        { return m_hovered; }
void UIButton::setHovered(bool hovered) { m_hovered = hovered; }

void UIButton::setOnClick(std::function<void()> callback) { m_onClick = std::move(callback); }
void UIButton::triggerClick()
{
    if (m_onClick)
        m_onClick();
}

const Font *UIButton::getFont() const { return m_fontLoaded ? &m_font : nullptr; }

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
