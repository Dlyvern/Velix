#ifndef ELIX_SCOPED_TIMER_HPP
#define ELIX_SCOPED_TIMER_HPP

#include "Core/Macros.hpp"
#include "Core/Logger.hpp"

#include <chrono>

class ScopedTimer
{
public:
    explicit ScopedTimer(const char *name)
        : m_name(name), m_start(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTimer()
    {
        const auto end = std::chrono::high_resolution_clock::now();
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start).count();

        VX_DEV_INFO_STREAM(m_name << " took: " << us << " us");
    }

private:
    const char *m_name;
    std::chrono::high_resolution_clock::time_point m_start;
};

#endif // ELIX_SCOPED_TIMER_HPP