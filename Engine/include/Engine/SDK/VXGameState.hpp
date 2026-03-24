#ifndef ELIX_VX_GAME_STATE_HPP
#define ELIX_VX_GAME_STATE_HPP

#include "Core/Macros.hpp"
#include "Core/Window.hpp"
#include "Engine/Scripting/VelixAPI.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Time.hpp"

#include <any>
#include <string>
#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Entity;
class Scene;

class VXGameState
{
public:
    static VXGameState &get()
    {
        static VXGameState s_instance;
        return s_instance;
    }

    Scene *getActiveScene() const { return scripting::getActiveScene(); }
    void loadScene(const std::string &path) { scripting::loadScene(path.c_str()); }
    void loadSceneAdditive(const std::string &path) { scripting::loadSceneAdditive(path.c_str()); }
    void unloadGroup(const std::string &tag) { scripting::unloadGroup(tag.c_str()); }
    void setDontDestroyOnLoad(Entity *entity) { scripting::setDontDestroyOnLoad(entity); }
    void clearDontDestroyOnLoad(Entity *entity) { scripting::clearDontDestroyOnLoad(entity); }

    float getDeltaTime() const { return Time::deltaTime(); }
    float getTimeSinceStart() const { return Time::totalTime(); }
    uint64_t getFrameCount() const { return Time::frameCount(); }
    float getTimeScale() const { return Time::timeScale(); }
    void setTimeScale(float s) { Time::setTimeScale(s); }

    RenderQualitySettings &getRenderSettings() const { return RenderQualitySettings::getInstance(); }

    void setFXAA(bool enable)
    {
        auto &s = RenderQualitySettings::getInstance();
        s.enableFXAA = enable;
    }

    void setBloom(bool enable)
    {
        RenderQualitySettings::getInstance().enableBloom = enable;
    }

    void setShadowQuality(RenderQualitySettings::ShadowQuality q)
    {
        RenderQualitySettings::getInstance().shadowQuality = q;
    }

    void setAntiAliasing(RenderQualitySettings::AntiAliasingMode mode)
    {
        RenderQualitySettings::getInstance().setAntiAliasingMode(mode);
    }

    void setVSync(bool enable)
    {
        RenderQualitySettings::getInstance().enableVSync = enable;
    }

    void setSSAO(bool enable)
    {
        RenderQualitySettings::getInstance().enableSSAO = enable;
    }

    void setRenderScale(float scale)
    {
        RenderQualitySettings::getInstance().renderScale = scale;
    }

    void setPostProcessing(bool enable)
    {
        RenderQualitySettings::getInstance().enablePostProcessing = enable;
    }

    void setFullscreen(bool enable)
    {
        if (auto *w = scripting::getActiveWindow())
        {
            w->setFullscreen(enable);
            m_isFullscreen = enable;
        }
    }

    bool isFullscreen() const { return m_isFullscreen; }

    void setWindowTitle(const std::string &title)
    {
        if (auto *w = scripting::getActiveWindow())
            w->setTitle(title);
    }

    void setWindowSize(int width, int height)
    {
        if (auto *w = scripting::getActiveWindow())
            w->setSize(width, height);
    }

    void maximizeWindow()
    {
        if (auto *w = scripting::getActiveWindow())
            w->maximize();
    }

    void restoreWindow()
    {
        if (auto *w = scripting::getActiveWindow())
            w->restore();
    }

    bool isWindowMaximized() const
    {
        if (auto *w = scripting::getActiveWindow())
            return w->isMaximized();
        return false;
    }

    uint32_t getWindowWidth() const
    {
        if (auto *w = scripting::getActiveWindow())
        {
            int width = 0, height = 0;
            w->getSize(&width, &height);
            return static_cast<uint32_t>(width);
        }
        return 0;
    }

    uint32_t getWindowHeight() const
    {
        if (auto *w = scripting::getActiveWindow())
        {
            int width = 0, height = 0;
            w->getSize(&width, &height);
            return static_cast<uint32_t>(height);
        }
        return 0;
    }

    template <typename T>
    void setGlobal(const std::string &key, T value)
    {
        m_globals[key] = std::any(std::move(value));
    }

    template <typename T>
    T getGlobal(const std::string &key, T fallback = {}) const
    {
        const auto it = m_globals.find(key);
        if (it == m_globals.end())
            return fallback;
        const T *val = std::any_cast<T>(&it->second);
        return val ? *val : fallback;
    }

    bool hasGlobal(const std::string &key) const { return m_globals.contains(key); }
    void removeGlobal(const std::string &key) { m_globals.erase(key); }

private:
    VXGameState() = default;

    bool m_isFullscreen{false};
    std::unordered_map<std::string, std::any> m_globals;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_VX_GAME_STATE_HPP
