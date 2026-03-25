#ifndef ELIX_ENVIRONMENT_SETTINGS_HPP
#define ELIX_ENVIRONMENT_SETTINGS_HPP

#include "Core/Macros.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct FogSettings
{
    bool enabled{false};
    glm::vec3 color{0.74f, 0.77f, 0.82f};
    float density{0.025f};
    float startDistance{0.0f};
    float maxOpacity{0.55f};
    float heightBase{0.0f};
    float heightFalloff{0.08f};
    float anisotropy{0.35f};
    float shaftIntensity{1.0f};
    float dustAmount{0.2f};
    float noiseScale{0.08f};
    float noiseScrollSpeed{0.025f};
};

struct SceneEnvironmentSettings
{
    std::string skyboxHDRPath;
    FogSettings fog{};
};

inline size_t hashFogSettings(const FogSettings &settings)
{
    size_t seed = 0u;

    auto hashCombine = [&seed](const auto &value)
    {
        seed ^= std::hash<std::decay_t<decltype(value)>>{}(value) + 0x9e3779b97f4a7c15ULL + (seed << 6u) + (seed >> 2u);
    };

    hashCombine(settings.enabled);
    hashCombine(settings.color.x);
    hashCombine(settings.color.y);
    hashCombine(settings.color.z);
    hashCombine(settings.density);
    hashCombine(settings.startDistance);
    hashCombine(settings.maxOpacity);
    hashCombine(settings.heightBase);
    hashCombine(settings.heightFalloff);
    hashCombine(settings.anisotropy);
    hashCombine(settings.shaftIntensity);
    hashCombine(settings.dustAmount);
    hashCombine(settings.noiseScale);
    hashCombine(settings.noiseScrollSpeed);

    return seed;
}

inline size_t hashSceneEnvironmentSettings(const SceneEnvironmentSettings &settings)
{
    size_t seed = hashFogSettings(settings.fog);
    seed ^= std::hash<std::string>{}(settings.skyboxHDRPath) + 0x9e3779b97f4a7c15ULL + (seed << 6u) + (seed >> 2u);
    return seed;
}

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ENVIRONMENT_SETTINGS_HPP
