#include "Engine/Time.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Time &Time::instance()
{
    static Time s_instance;
    return s_instance;
}

void Time::update(float deltaTime)
{
    m_deltaTime = deltaTime;
    m_totalTime += deltaTime;
    ++m_frameCount;
}

float Time::deltaTime()
{
    return instance().m_deltaTime;
}

float Time::totalTime()
{
    return instance().m_totalTime;
}

uint64_t Time::frameCount()
{
    return instance().m_frameCount;
}

float Time::timeScale()
{
    return instance().m_timeScale;
}

void Time::setTimeScale(float scale)
{
    instance().m_timeScale = scale < 0.0f ? 0.0f : scale;
}

float Time::scaledDeltaTime()
{
    auto &inst = instance();
    return inst.m_deltaTime * inst.m_timeScale;
}

ELIX_NESTED_NAMESPACE_END
