#ifndef ELIX_SDK_EDITOR_PLUGIN_HPP
#define ELIX_SDK_EDITOR_PLUGIN_HPP

#include "VelixSDK/Plugin.hpp"

#include "Engine/Camera.hpp"
#include "Engine/Entity.hpp"
#include "Engine/Scene.hpp"

#include <filesystem>
#include <glm/vec2.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(sdk)

struct EditorContext
{
    elix::engine::Scene *scene{nullptr};

    const std::filesystem::path *projectRootPath{nullptr};

    elix::engine::Entity *selectedEntity{nullptr};

    const elix::engine::Camera *editorCamera{nullptr};

    // Frame delta time in seconds.
    float deltaTime{0.0f};

    bool wantsBrushInput{false};

    bool brushStrokeActive{false};
    bool brushStrokeStart{false};
    glm::vec2 brushNdcPosition{0.0f, 0.0f};
};

class IEditorPlugin : public VXPlugin
{
public:
    ~IEditorPlugin() override = default;

    // Called once per ImGui frame between ImGui::NewFrame() and ImGui::Render().
    // Draw all ImGui windows for this plugin here.
    virtual void onEditorFrame(EditorContext &ctx) = 0;

    // Optional: return a non-null string to get a toggle button in the editor
    // bottom panel. The label should be short (fits on a toolbar button).
    virtual const char *getToolbarButtonLabel() const { return nullptr; }

    // Called when the user clicks the toolbar button returned by getToolbarButtonLabel().
    virtual void toggleToolbarWindow() {}
};

ELIX_NESTED_NAMESPACE_END

// Use this macro instead of REGISTER_PLUGIN for editor plugins.
// It generates the standard createPlugin / destroyPlugin C symbols.
#define REGISTER_EDITOR_PLUGIN(PluginClass)                                                             \
    extern "C" ELIX_PLUGIN_EXPORT ::elix::engine::IPlugin *createPlugin() { return new PluginClass(); } \
    extern "C" ELIX_PLUGIN_EXPORT void destroyPlugin(::elix::engine::IPlugin *p) { delete p; }

#endif // ELIX_SDK_EDITOR_PLUGIN_HPP
