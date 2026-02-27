#ifndef ELIX_AUDIO_MUSIC_HPP
#define ELIX_AUDIO_MUSIC_HPP

#include "Core/Macros.hpp"

namespace FMOD
{
    class Sound;
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(audio)

class AudioMusic
{
public:
    bool create(const std::string &fileName);
    void destroy();

    ~AudioMusic();

private:
    FMOD::Sound *m_sound{nullptr};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_AUDIO_MUSIC_HPP