#include "Engine/Components/ParticleSystemComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Entity.hpp"
#include "Engine/Scripting/VelixAPI.hpp"
#include "Engine/Scene.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void ParticleSystemComponent::onAttach()
{
    if (playOnStart && m_particleSystem)
        m_particleSystem->play();
}

void ParticleSystemComponent::onDetach()
{
    if (m_particleSystem)
        m_particleSystem->stop();
}

void ParticleSystemComponent::update(float dt)
{
    if (!m_particleSystem)
        return;

    if (playOnStart && !m_started)
    {
        m_particleSystem->play();
        m_started = true;
    }

    glm::vec3 worldPos{0.0f};
    if (auto *entity = getOwner<Entity>())
    {
        if (auto *transform = entity->getComponent<Transform3DComponent>())
            worldPos = transform->getWorldPosition();
    }

    if (auto *scene = scripting::getActiveScene())
        m_particleSystem->setPhysicsScene(&scene->getPhysicsScene());

    m_particleSystem->update(dt, worldPos);
}

void ParticleSystemComponent::setParticleSystem(ParticleSystem::SharedPtr system)
{
    m_particleSystem = std::move(system);
    m_started = false;
}

ParticleSystem *ParticleSystemComponent::getParticleSystem() const
{
    return m_particleSystem.get();
}

void ParticleSystemComponent::play()
{
    if (m_particleSystem) m_particleSystem->play();
}

void ParticleSystemComponent::stop()
{
    if (m_particleSystem) m_particleSystem->stop();
}

void ParticleSystemComponent::pause()
{
    if (m_particleSystem) m_particleSystem->pause();
}

void ParticleSystemComponent::reset()
{
    if (m_particleSystem) m_particleSystem->reset();
    m_started = false;
}

bool ParticleSystemComponent::isPlaying() const
{
    return m_particleSystem && m_particleSystem->isPlaying();
}

ELIX_NESTED_NAMESPACE_END
