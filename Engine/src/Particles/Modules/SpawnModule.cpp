#include "Engine/Particles/Modules/SpawnModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

#include <glm/gtc/constants.hpp>
#include <cmath>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void SpawnModule::onParticleSpawn(Particle &particle)
{
    particle.position = samplePosition(m_emitterWorldPos);
}

glm::vec3 SpawnModule::samplePosition(const glm::vec3 &emitterWorldPos) const
{
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> distAngle(0.0f, glm::two_pi<float>());

    switch (shape.shape)
    {
    case EmitterShape::Point:
        return emitterWorldPos;

    case EmitterShape::Sphere:
    {
        // Uniform sphere / shell sampling
        const float u = dist01(m_rng);
        const float v = dist01(m_rng);
        const float phi = distAngle(m_rng);
        const float theta = std::acos(1.0f - 2.0f * u);
        const float r = shape.surfaceOnly ? 1.0f : std::cbrt(v); // cube-root for volume
        return emitterWorldPos + shape.radius * r * glm::vec3(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
    }

    case EmitterShape::Box:
    {
        std::uniform_real_distribution<float> dx(-shape.extents.x, shape.extents.x);
        std::uniform_real_distribution<float> dy(-shape.extents.y, shape.extents.y);
        std::uniform_real_distribution<float> dz(-shape.extents.z, shape.extents.z);
        return emitterWorldPos + glm::vec3(dx(m_rng), dy(m_rng), dz(m_rng));
    }

    case EmitterShape::Cone:
    {
        const float angleRad = glm::radians(shape.angle);
        const float t = dist01(m_rng); // along height
        const float rAtT = t * shape.height * std::tan(angleRad);
        const float phi = distAngle(m_rng);
        const float r2 = shape.surfaceOnly ? rAtT : rAtT * std::sqrt(dist01(m_rng));
        return emitterWorldPos + glm::vec3(
                                     r2 * std::cos(phi),
                                     t * shape.height,
                                     r2 * std::sin(phi));
    }

    case EmitterShape::Cylinder:
    {
        const float phi = distAngle(m_rng);
        const float r = shape.surfaceOnly ? 1.0f : std::sqrt(dist01(m_rng));
        const float h = (dist01(m_rng) - 0.5f) * shape.height;
        return emitterWorldPos + glm::vec3(
                                     shape.radius * r * std::cos(phi),
                                     h,
                                     shape.radius * r * std::sin(phi));
    }
    }

    return emitterWorldPos;
}

glm::vec3 SpawnModule::sampleDirection() const
{
    // Cone-shaped direction spread (useful when InitialVelocityModule is absent)
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> distAngle(0.0f, glm::two_pi<float>());

    const float angleRad = glm::radians(shape.angle);
    const float phi = distAngle(m_rng);
    const float cosTheta = 1.0f - dist01(m_rng) * (1.0f - std::cos(angleRad));
    const float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
    return glm::normalize(glm::vec3(
        sinTheta * std::cos(phi),
        cosTheta,
        sinTheta * std::sin(phi)));
}

ELIX_NESTED_NAMESPACE_END
