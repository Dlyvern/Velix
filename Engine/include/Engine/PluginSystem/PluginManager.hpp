#ifndef ELIX_PLUGIN_MANAGER_HPP
#define ELIX_PLUGIN_MANAGER_HPP

#include "Core/Macros.hpp"
#include "Engine/PluginSystem/IPlugin.hpp"
#include "Engine/PluginSystem/PluginLoader.hpp"

#include <filesystem>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class PluginManager
{
public:
    static PluginManager &instance();

    // Scan directory for .so/.dll files and load each as a plugin.
    // Also merges any scripts exported via getScriptsRegister() into ScriptsRegister::instance().
    void loadPluginsFromDirectory(const std::filesystem::path &pluginsDir);

    void unloadAll();

    const std::vector<IPlugin *> &getLoadedPlugins() const;

private:
    struct PluginEntry
    {
        std::string filename; // basename, for duplicate detection
        LibraryHandle handle{nullptr};
        IPlugin *plugin{nullptr};              // nullptr if no createPlugin symbol
        void (*destroyer)(IPlugin *){nullptr}; // nullptr if no destroyPlugin symbol
    };

    std::vector<PluginEntry> m_plugins;
    std::vector<IPlugin *> m_pluginPtrs; // kept in sync for getLoadedPlugins()
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PLUGIN_MANAGER_HPP
