#ifndef ELIX_PARTICLE_SYSTEM_COMPONENT_HPP
#define ELIX_PARTICLE_SYSTEM_COMPONENT_HPP

#include "Core/Macros.hpp"

#include "Engine/Components/ECS.hpp"
#include "Engine/Particles/ParticleSystem.hpp"

#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

/// ECS component that drives a ParticleSystem.
/// Registered as a multi-component so one entity can carry multiple effects.
class ParticleSystemComponent final : public ECS
{
public:
    void onAttach() override;
    void onDetach() override;
    void update(float dt) override;

    void setParticleSystem(ParticleSystem::SharedPtr system);
    ParticleSystem *getParticleSystem() const;

    void play();
    void stop();
    void pause();
    void reset();

    bool isPlaying() const;

    bool loadFromAsset(const std::string &path);
    bool saveToAsset(const std::string &path) const;

    bool playOnStart{true};
    std::string vfxAssetPath;

private:
    ParticleSystem::SharedPtr m_particleSystem;
    bool m_started{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PARTICLE_SYSTEM_COMPONENT_HPP
