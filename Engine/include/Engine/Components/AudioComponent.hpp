#ifndef ELIX_AUDIO_COMPONENT_HPP
#define ELIX_AUDIO_COMPONENT_HPP

#include "Core/Macros.hpp"

#include "Engine/Components/ECS.hpp"

#include <string>
#include <vector>
#include <cstdint>

namespace FMOD
{
    class Sound;
    class Channel;
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class AudioComponent final : public ECS
{
public:
    enum class AudioType : uint8_t
    {
        Sound = 0,
        Music = 1
    };

    void onAttach() override;
    void onDetach() override;
    void update(float deltaTime) override;

    bool loadFromAsset(const std::string &assetPath);
    void clearAudio();

    const std::string &getAssetPath() const;

    void play();
    void stop();
    void pause();
    void resume();

    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isPaused() const;

    void setVolume(float volume);
    [[nodiscard]] float getVolume() const;

    void setPitch(float pitch);
    [[nodiscard]] float getPitch() const;

    void setLooping(bool loop);
    [[nodiscard]] bool isLooping() const;

    void setPlayOnStart(bool playOnStart);
    [[nodiscard]] bool isPlayOnStart() const;

    void setMuted(bool muted);
    [[nodiscard]] bool isMuted() const;

    void setAudioType(AudioType type);
    [[nodiscard]] AudioType getAudioType() const;

    void setMinDistance(float minDistance);
    [[nodiscard]] float getMinDistance() const;

    void setMaxDistance(float maxDistance);
    [[nodiscard]] float getMaxDistance() const;

    void setSpatial(bool spatial);
    [[nodiscard]] bool isSpatial() const;

private:
    void createFmodSound();
    void destroyFmodSound();
    void applyChannelSettings();

    std::string m_assetPath;
    std::vector<uint8_t> m_audioData;

    FMOD::Sound *m_sound{nullptr};
    FMOD::Channel *m_channel{nullptr};

    float m_volume{1.0f};
    float m_pitch{1.0f};
    float m_minDistance{1.0f};
    float m_maxDistance{500.0f};

    bool m_loop{false};
    bool m_playOnStart{false};
    bool m_muted{false};
    bool m_spatial{false};
    bool m_startedPlaying{false};

    AudioType m_audioType{AudioType::Sound};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_AUDIO_COMPONENT_HPP
