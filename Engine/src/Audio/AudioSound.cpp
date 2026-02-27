#include "Engine/Audio/AudioSound.hpp"
#include "Engine/Audio/AudioSystem.hpp"

#include "fmod.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(audio)

void AudioSound::play()
{
    if (!m_sound)
        return;

    m_channel = nullptr;

    auto result = AudioSystem::getInstance()->getSystem()->playSound(
        m_sound,
        nullptr,
        false,
        &m_channel);

    if (result != FMOD_OK)
    {
        m_channel = nullptr;
    }
}

bool AudioSound::create(const std::string &fileName)
{
    if (m_sound)
        destroy();

    auto result = AudioSystem::getInstance()->getSystem()->createSound(fileName.c_str(), FMOD_DEFAULT, nullptr, &m_sound);

    return result == FMOD_OK;
}

void AudioSound::destroy()
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

AudioSound::~AudioSound()
{
    destroy();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
