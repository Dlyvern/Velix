#ifndef ELIX_TIME_HPP
#define ELIX_TIME_HPP

#include "Core/Macros.hpp"

#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Time
{
public:
    static Time &instance();


    void update(float deltaTime);


    static float deltaTime();


    static float totalTime();


    static uint64_t frameCount();


    static float timeScale();
    static void setTimeScale(float scale);


    static float scaledDeltaTime();

private:
    Time() = default;

    float m_deltaTime{0.0f};
    float m_totalTime{0.0f};
    float m_timeScale{1.0f};
    uint64_t m_frameCount{0};
};

ELIX_NESTED_NAMESPACE_END

#endif
