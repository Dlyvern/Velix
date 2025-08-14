#ifndef UI_FADE_ANIMATION_HPP
#define UI_FADE_ANIMATION_HPP

#include "VelixFlow/UI/UIWidget.hpp"

class UIFadeAnimation
{
public:
    UIFadeAnimation(std::shared_ptr<elix::ui::UIWidget> target, float duration, float toAlpha = 0.0f);

    void update(float deltaTime);

    bool isFinished() const;
    
private:
    std::shared_ptr<elix::ui::UIWidget> m_target{nullptr};
    float m_duration;
    float m_elapsed{0.0f};
    bool m_isFinished{false};
    float m_startAlpha{0.0f};
    float m_endAlpha{0.0f};
};


#endif //UI_FADE_ANIMATION_HPP