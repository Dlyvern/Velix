#ifndef ELIX_AUDIO_SOUND_HPP
#define ELIX_AUDIO_SOUND_HPP

#include "Core/Macros.hpp"

namespace FMOD
{
    class Sound;
    class Channel;
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(audio)

class AudioSound
{
public:
    bool create(const std::string &fileName);
    void destroy();
    void play();

    ~AudioSound();

private:
    FMOD::Sound *m_sound{nullptr};
    FMOD::Channel *m_channel{nullptr};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_AUDIO_SOUND_HPP