#ifndef ELIX_PARTICLE_TYPES_HPP
#define ELIX_PARTICLE_TYPES_HPP

#include "Core/Macros.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <vector>
#include <algorithm>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct CurvePoint
{
    float time{0.0f}; // normalised age 0..1
    float value{1.0f};
};

inline float evaluateCurve(const std::vector<CurvePoint> &curve, float t)
{
    if (curve.empty())
        return 1.0f;
    if (curve.size() == 1)
        return curve[0].value;

    t = std::clamp(t, 0.0f, 1.0f);

    for (size_t i = 0; i + 1 < curve.size(); ++i)
    {
        if (t <= curve[i + 1].time)
        {
            const float span = curve[i + 1].time - curve[i].time;
            const float f = (span > 0.0f) ? (t - curve[i].time) / span : 0.0f;
            return glm::mix(curve[i].value, curve[i + 1].value, f);
        }
    }
    return curve.back().value;
}

struct GradientPoint
{
    float time{0.0f};
    glm::vec4 color{1.0f};
};

inline glm::vec4 evaluateGradient(const std::vector<GradientPoint> &gradient, float t)
{
    if (gradient.empty())
        return glm::vec4(1.0f);
    if (gradient.size() == 1)
        return gradient[0].color;

    t = std::clamp(t, 0.0f, 1.0f);

    for (size_t i = 0; i + 1 < gradient.size(); ++i)
    {
        if (t <= gradient[i + 1].time)
        {
            const float span = gradient[i + 1].time - gradient[i].time;
            const float f = (span > 0.0f) ? (t - gradient[i].time) / span : 0.0f;
            return glm::mix(gradient[i].color, gradient[i + 1].color, f);
        }
    }
    return gradient.back().color;
}

struct Particle
{
    glm::vec3 position{0.0f};
    float rotation{0.0f}; // billboard rotation in radians

    glm::vec3 velocity{0.0f};
    float rotationSpeed{0.0f}; // rad/s

    glm::vec4 color{1.0f};

    glm::vec2 size{1.0f}; // width / height in world units

    float age{0.0f};
    float lifetime{1.0f};

    bool alive{false};

    float getNormalizedAge() const
    {
        return lifetime > 0.0f ? std::clamp(age / lifetime, 0.0f, 1.0f) : 1.0f;
    }

    bool isDead() const { return age >= lifetime; }
};

struct ParticleGPUData
{
    glm::vec4 positionAndRotation; // xyz = world pos, w = rotation (rad)
    glm::vec4 color;               // rgba
    glm::vec2 size;                // width, height
    glm::vec2 _pad;
};

static_assert(sizeof(ParticleGPUData) == 48, "ParticleGPUData layout changed");

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PARTICLE_TYPES_HPP
