#include "UIFadeAnimation.hpp"

UIFadeAnimation::UIFadeAnimation(std::shared_ptr<elix::ui::UIWidget> target, float duration, float toAlpha) : m_target(target), m_duration(duration)
{
    m_endAlpha = toAlpha;

    if(m_target)
        m_startAlpha = m_target->getAlpha();
}

bool UIFadeAnimation::isFinished() const
{
    return m_isFinished;
}

void UIFadeAnimation::update(float deltaTime)
{
    if(m_isFinished || !m_target)
        return;

    m_elapsed += deltaTime;

    float progress = m_elapsed / m_duration;

    if(progress > 1.0f)
        progress = 1.0f;

    float alpha = m_startAlpha + (m_endAlpha - m_startAlpha) * progress;
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    m_target->setAlpha(alpha);

    if(progress >= 1.0f)
        m_isFinished = true;
}
