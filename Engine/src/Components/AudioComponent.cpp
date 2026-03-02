#include "Engine/Components/AudioComponent.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Audio/AudioSystem.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Entity.hpp"

#include "Core/Logger.hpp"

#include "fmod.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void AudioComponent::onAttach()
{
    if (!m_assetPath.empty() && !m_audioData.empty())
        createFmodSound();

    m_startedPlaying = false;
}

void AudioComponent::onDetach()
{
    destroyFmodSound();
}

void AudioComponent::update(float deltaTime)
{
    if (!m_startedPlaying && m_playOnStart && m_sound)
    {
        m_startedPlaying = true;
        play();
    }

    if (m_spatial && m_channel && m_sound)
    {
        auto *owner = getOwner<Entity>();
        if (owner)
        {
            auto *transformComponent = owner->getComponent<Transform3DComponent>();
            if (transformComponent)
            {
                const glm::vec3 worldPos = transformComponent->getWorldPosition();
                FMOD_VECTOR pos{worldPos.x, worldPos.y, worldPos.z};
                FMOD_VECTOR vel{0.0f, 0.0f, 0.0f};
                m_channel->set3DAttributes(&pos, &vel);
            }
        }
    }

    auto *system = audio::AudioSystem::getInstance();
    if (system && system->getSystem())
        system->getSystem()->update();
}

bool AudioComponent::loadFromAsset(const std::string &assetPath)
{
    destroyFmodSound();

    m_assetPath = assetPath;
    m_audioData.clear();

    auto audioAsset = AssetsLoader::loadAudio(assetPath);
    if (!audioAsset.has_value())
    {
        VX_ENGINE_ERROR_STREAM("AudioComponent: Failed to load audio asset: " << assetPath << '\n');
        m_assetPath.clear();
        return false;
    }

    m_audioData = std::move(audioAsset->audioData);

    createFmodSound();
    return m_sound != nullptr;
}

void AudioComponent::clearAudio()
{
    destroyFmodSound();
    m_assetPath.clear();
    m_audioData.clear();
    m_startedPlaying = false;
}

const std::string &AudioComponent::getAssetPath() const
{
    return m_assetPath;
}

void AudioComponent::play()
{
    if (!m_sound)
        return;

    auto *system = audio::AudioSystem::getInstance();
    if (!system || !system->getSystem())
        return;

    if (m_channel)
    {
        bool isPlaying = false;
        m_channel->isPlaying(&isPlaying);
        if (isPlaying)
            m_channel->stop();
        m_channel = nullptr;
    }

    FMOD_RESULT result = system->getSystem()->playSound(m_sound, nullptr, true, &m_channel);
    if (result != FMOD_OK || !m_channel)
    {
        m_channel = nullptr;
        return;
    }

    applyChannelSettings();
    m_channel->setPaused(false);
}

void AudioComponent::stop()
{
    if (!m_channel)
        return;

    m_channel->stop();
    m_channel = nullptr;
}

void AudioComponent::pause()
{
    if (!m_channel)
        return;

    m_channel->setPaused(true);
}

void AudioComponent::resume()
{
    if (!m_channel)
        return;

    m_channel->setPaused(false);
}

bool AudioComponent::isPlaying() const
{
    if (!m_channel)
        return false;

    bool playing = false;
    m_channel->isPlaying(&playing);
    bool paused = false;
    m_channel->getPaused(&paused);
    return playing && !paused;
}

bool AudioComponent::isPaused() const
{
    if (!m_channel)
        return false;

    bool paused = false;
    m_channel->getPaused(&paused);
    return paused;
}

void AudioComponent::setVolume(float volume)
{
    m_volume = volume;
    if (m_channel)
        m_channel->setVolume(m_muted ? 0.0f : m_volume);
}

float AudioComponent::getVolume() const
{
    return m_volume;
}

void AudioComponent::setPitch(float pitch)
{
    m_pitch = pitch;
    if (m_channel)
        m_channel->setPitch(m_pitch);
}

float AudioComponent::getPitch() const
{
    return m_pitch;
}

void AudioComponent::setLooping(bool loop)
{
    m_loop = loop;
    destroyFmodSound();
    if (!m_audioData.empty())
        createFmodSound();
}

bool AudioComponent::isLooping() const
{
    return m_loop;
}

void AudioComponent::setPlayOnStart(bool playOnStart)
{
    m_playOnStart = playOnStart;
}

bool AudioComponent::isPlayOnStart() const
{
    return m_playOnStart;
}

void AudioComponent::setMuted(bool muted)
{
    m_muted = muted;
    if (m_channel)
        m_channel->setVolume(m_muted ? 0.0f : m_volume);
}

bool AudioComponent::isMuted() const
{
    return m_muted;
}

void AudioComponent::setAudioType(AudioType type)
{
    m_audioType = type;
}

AudioComponent::AudioType AudioComponent::getAudioType() const
{
    return m_audioType;
}

void AudioComponent::setMinDistance(float minDistance)
{
    m_minDistance = minDistance;
    if (m_sound)
        m_sound->set3DMinMaxDistance(m_minDistance, m_maxDistance);
}

float AudioComponent::getMinDistance() const
{
    return m_minDistance;
}

void AudioComponent::setMaxDistance(float maxDistance)
{
    m_maxDistance = maxDistance;
    if (m_sound)
        m_sound->set3DMinMaxDistance(m_minDistance, m_maxDistance);
}

float AudioComponent::getMaxDistance() const
{
    return m_maxDistance;
}

void AudioComponent::setSpatial(bool spatial)
{
    m_spatial = spatial;
    destroyFmodSound();
    if (!m_audioData.empty())
        createFmodSound();
}

bool AudioComponent::isSpatial() const
{
    return m_spatial;
}

void AudioComponent::createFmodSound()
{
    if (m_audioData.empty())
        return;

    auto *system = audio::AudioSystem::getInstance();
    if (!system || !system->getSystem())
        return;

    FMOD_CREATESOUNDEXINFO exinfo{};
    exinfo.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
    exinfo.length = static_cast<unsigned int>(m_audioData.size());

    FMOD_MODE mode = FMOD_OPENMEMORY;

    if (m_loop)
        mode |= FMOD_LOOP_NORMAL;
    else
        mode |= FMOD_LOOP_OFF;

    if (m_spatial)
        mode |= FMOD_3D;
    else
        mode |= FMOD_2D;

    FMOD_RESULT result = system->getSystem()->createSound(
        reinterpret_cast<const char *>(m_audioData.data()),
        mode,
        &exinfo,
        &m_sound);

    if (result != FMOD_OK)
    {
        VX_ENGINE_ERROR_STREAM("AudioComponent: FMOD failed to create sound from asset: " << m_assetPath << '\n');
        m_sound = nullptr;
        return;
    }

    if (m_spatial)
        m_sound->set3DMinMaxDistance(m_minDistance, m_maxDistance);
}

void AudioComponent::destroyFmodSound()
{
    if (m_channel)
    {
        m_channel->stop();
        m_channel = nullptr;
    }

    if (m_sound)
    {
        m_sound->release();
        m_sound = nullptr;
    }
}

void AudioComponent::applyChannelSettings()
{
    if (!m_channel)
        return;

    m_channel->setVolume(m_muted ? 0.0f : m_volume);
    m_channel->setPitch(m_pitch);

    if (m_spatial)
    {
        auto *owner = getOwner<Entity>();
        if (owner)
        {
            auto *transformComponent = owner->getComponent<Transform3DComponent>();
            if (transformComponent)
            {
                const glm::vec3 worldPos = transformComponent->getWorldPosition();
                FMOD_VECTOR pos{worldPos.x, worldPos.y, worldPos.z};
                FMOD_VECTOR vel{0.0f, 0.0f, 0.0f};
                m_channel->set3DAttributes(&pos, &vel);
            }
        }
    }
}

ELIX_NESTED_NAMESPACE_END
