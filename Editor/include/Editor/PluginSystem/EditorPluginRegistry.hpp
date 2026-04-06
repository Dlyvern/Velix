#ifndef ELIX_EDITOR_PLUGIN_REGISTRY_HPP
#define ELIX_EDITOR_PLUGIN_REGISTRY_HPP

#include "VelixSDK/EditorPlugin.hpp"

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

// Tracks all loaded IEditorPlugin instances and dispatches frame callbacks.
// Populated from EditorRuntime::init() after plugin loading, by dynamic_cast-ing
// each loaded IPlugin* to IEditorPlugin*.
class EditorPluginRegistry
{
public:
    static EditorPluginRegistry &instance();

    void registerEditorPlugin(elix::sdk::IEditorPlugin *plugin);
    void unregisterAll();

    // Called once per ImGui frame (from Editor::drawFrame) to dispatch
    // onEditorFrame to every registered plugin.
    void dispatchFrame(elix::sdk::EditorContext &ctx);

    const std::vector<elix::sdk::IEditorPlugin *> &getPlugins() const;

private:
    EditorPluginRegistry() = default;

    std::vector<elix::sdk::IEditorPlugin *> m_plugins;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_PLUGIN_REGISTRY_HPP
