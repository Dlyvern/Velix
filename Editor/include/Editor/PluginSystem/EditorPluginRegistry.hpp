#ifndef ELIX_EDITOR_PLUGIN_REGISTRY_HPP
#define ELIX_EDITOR_PLUGIN_REGISTRY_HPP

#include "VelixSDK/EditorPlugin.hpp"

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)




class EditorPluginRegistry
{
public:
    static EditorPluginRegistry &instance();

    void registerEditorPlugin(elix::sdk::IEditorPlugin *plugin);
    void unregisterAll();



    void dispatchFrame(elix::sdk::EditorContext &ctx);

    const std::vector<elix::sdk::IEditorPlugin *> &getPlugins() const;


    bool anyPluginWantsBrush() const { return m_lastFrameWantsBrush; }

private:
    EditorPluginRegistry() = default;

    std::vector<elix::sdk::IEditorPlugin *> m_plugins;
    bool m_lastFrameWantsBrush{false};
};

ELIX_NESTED_NAMESPACE_END

#endif
