#ifndef ELIX_AUDIO_SYSTEM_HPP
#define ELIX_AUDIO_SYSTEM_HPP

#include "Core/Macros.hpp"

namespace FMOD
{
    class System;
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(audio)

class AudioSystem
{
public:
    AudioSystem(const AudioSystem &) = delete;
    AudioSystem &operator=(const AudioSystem &) = delete;
    AudioSystem(AudioSystem &&) noexcept = default;
    AudioSystem &operator=(AudioSystem &&) noexcept = default;

    static bool init();

    static void shutdown();

    static AudioSystem *getInstance();

    FMOD::System *const getSystem();

    AudioSystem() = default;

private:
    static inline std::unique_ptr<AudioSystem> s_instance{nullptr};

    FMOD::System *m_system{nullptr};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_AUDIO_SYSTEM_HPP