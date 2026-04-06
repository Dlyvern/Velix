#include "Editor/PluginSystem/EditorPluginRegistry.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

EditorPluginRegistry &EditorPluginRegistry::instance()
{
    static EditorPluginRegistry s_instance;
    return s_instance;
}

void EditorPluginRegistry::registerEditorPlugin(elix::sdk::IEditorPlugin *plugin)
{
    if (plugin)
        m_plugins.push_back(plugin);
}

void EditorPluginRegistry::unregisterAll()
{
    m_plugins.clear();
}

void EditorPluginRegistry::dispatchFrame(elix::sdk::EditorContext &ctx)
{
    for (auto *plugin : m_plugins)
    {
        if (plugin)
            plugin->onEditorFrame(ctx);
    }
}

const std::vector<elix::sdk::IEditorPlugin *> &EditorPluginRegistry::getPlugins() const
{
    return m_plugins;
}

ELIX_NESTED_NAMESPACE_END
