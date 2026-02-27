#include "Engine/Audio/AudioSystem.hpp"
#include "Core/Logger.hpp"

#include "fmod.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(audio)

bool AudioSystem::init()
{
    if (!s_instance)
        s_instance = std::make_unique<AudioSystem>();

    FMOD_RESULT result;

    result = FMOD::System_Create(&s_instance->m_system);

    if (result != FMOD_OK || !s_instance->m_system)
    {
        VX_ENGINE_ERROR_STREAM("Failed to create FMOD system");
        return false;
    }

    result = s_instance->m_system->init(512, FMOD_INIT_NORMAL, nullptr);

    if (result != FMOD_OK)
    {
        VX_ENGINE_ERROR_STREAM("Failed to init FMOD");
        return false;
    }

    return true;
}

void AudioSystem::shutdown()
{
    s_instance->m_system->close();
    s_instance->m_system->release();
}

FMOD::System *const AudioSystem::getSystem()
{
    return m_system;
}

AudioSystem *AudioSystem::getInstance()
{
    return s_instance.get();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
