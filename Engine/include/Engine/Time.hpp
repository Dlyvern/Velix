#ifndef ELIX_TIME_HPP
#define ELIX_TIME_HPP

#include "Core/Macros.hpp"

#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Time
{
public:
    static Time &instance();

    // Called once per frame by the engine before scripts tick
    void update(float deltaTime);

    // Seconds elapsed since last frame
    static float deltaTime();

    // Total seconds elapsed since the engine started
    static float totalTime();

    // Number of frames rendered so far
    static uint64_t frameCount();

    // Multiplier applied to time — 1.0 = normal, 0.0 = paused, 0.5 = slow-motion
    static float timeScale();
    static void setTimeScale(float scale);

    // deltaTime * timeScale — use this for all gameplay movement
    static float scaledDeltaTime();

private:
    Time() = default;

    float m_deltaTime{0.0f};
    float m_totalTime{0.0f};
    float m_timeScale{1.0f};
    uint64_t m_frameCount{0};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_TIME_HPP
