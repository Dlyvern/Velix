#include "Engine/Audio/AudioMusic.hpp"
#include "Engine/Audio/AudioSystem.hpp"

#include "fmod.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(audio)

bool AudioMusic::create(const std::string &fileName)
{
    if (m_sound)
        destroy();

    auto result = AudioSystem::getInstance()->getSystem()->createSound(fileName.c_str(), FMOD_LOOP_NORMAL, nullptr, &m_sound);

    return result == FMOD_OK;
}

void AudioMusic::destroy()
{
    if (m_sound)
    {
        m_sound->release();
        m_sound = nullptr;
    }
}

AudioMusic::~AudioMusic()
{
    destroy();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END