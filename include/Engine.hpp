#ifndef ENGINE_HPP
#define ENGINE_HPP

#include "VelixFlow/Window.hpp"
#include "VelixFlow/Scene.hpp"
#include "VelixFlow/Components/CameraComponent.hpp"
#include "VelixFlow/RenderAPI/Interface/IRenderer.hpp"
#include "VelixFlow/RenderAPI/RenderAPI.hpp"

#include "EngineConfig.hpp"
#include "CrashHandler.hpp"
#include "Camera.hpp"
#include "Editor.hpp"

class Engine
{
public:
    static int run();

    static inline window::Window* s_window{nullptr};
    static inline std::unique_ptr<elix::Scene> s_scene{nullptr};
    static inline std::unique_ptr<elix::components::CameraComponent> s_camera{nullptr};
    static inline elix::IRenderer* s_renderer{nullptr};
    static inline elix::render::RenderAPI s_selectedRenderAPI{elix::render::RenderAPI::OpenGL};
    static inline elix::IRenderContext* s_renderContext{nullptr};
    static inline EngineConfig s_engineConfig;
private:
    static inline CrashHandler s_crashHandler;
    static inline Editor s_editor;

    static void init();

    static void initRenderAPI(elix::render::RenderAPI renderApi);
};


#endif //ENGINE_HPP
