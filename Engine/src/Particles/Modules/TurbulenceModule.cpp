#include "Engine/Particles/Modules/TurbulenceModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

#include <glm/glm.hpp>
#include <cmath>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace
{
    // Minimal hash-based value noise, no external dependencies.
    // Returns a pseudo-random float in [-1, 1] from integer seed.
    inline float hashFloat(int x, int y, int z)
    {
        int h = x * 1031 + y * 2999 + z * 4801;
        h = h ^ (h >> 13);
        h = h * (h * h * 15731 + 789221) + 1376312589;
        return static_cast<float>(h & 0x7fffffff) / static_cast<float>(0x7fffffff) * 2.0f - 1.0f;
    }

    // Trilinear interpolation of hash noise.
    inline float valueNoise3D(float x, float y, float z)
    {
        const int ix = static_cast<int>(std::floor(x));
        const int iy = static_cast<int>(std::floor(y));
        const int iz = static_cast<int>(std::floor(z));

        const float fx = x - static_cast<float>(ix);
        const float fy = y - static_cast<float>(iy);
        const float fz = z - static_cast<float>(iz);

        // Smoothstep
        const float ux = fx * fx * (3.0f - 2.0f * fx);
        const float uy = fy * fy * (3.0f - 2.0f * fy);
        const float uz = fz * fz * (3.0f - 2.0f * fz);

        const float c000 = hashFloat(ix,     iy,     iz    );
        const float c100 = hashFloat(ix + 1, iy,     iz    );
        const float c010 = hashFloat(ix,     iy + 1, iz    );
        const float c110 = hashFloat(ix + 1, iy + 1, iz    );
        const float c001 = hashFloat(ix,     iy,     iz + 1);
        const float c101 = hashFloat(ix + 1, iy,     iz + 1);
        const float c011 = hashFloat(ix,     iy + 1, iz + 1);
        const float c111 = hashFloat(ix + 1, iy + 1, iz + 1);

        return glm::mix(
            glm::mix(glm::mix(c000, c100, ux), glm::mix(c010, c110, ux), uy),
            glm::mix(glm::mix(c001, c101, ux), glm::mix(c011, c111, ux), uy),
            uz);
    }
}

void TurbulenceModule::onParticleUpdate(Particle &particle, float deltaTime)
{
    if (!m_enabled)
        return;

    // Sample three noise fields with different offsets for X/Y/Z axes.
    const glm::vec3 sp = particle.position * frequency;
    // elapsedTime proxy: use particle.age as a scrolling offset
    const float scroll = particle.age * scrollSpeed;

    const float nx = valueNoise3D(sp.x + scroll,        sp.y,                sp.z               );
    const float ny = valueNoise3D(sp.x,                 sp.y + scroll + 13.7f, sp.z             );
    const float nz = valueNoise3D(sp.x,                 sp.y,                sp.z + scroll + 7.3f);

    particle.velocity += glm::vec3(nx, ny, nz) * (strength * deltaTime);
}

ELIX_NESTED_NAMESPACE_END
