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
    enum class PluginCategory
    {
        Engine, // Always loaded — cannot be toggled off by the user.
        Custom  // User-managed — respects EngineConfig::isPluginEnabled().
    };

    struct PluginInfo
    {
        std::string pluginName; // from IPlugin::getName(), empty if no createPlugin symbol
        std::string filename;   // basename of the .so/.dll
        PluginCategory category{PluginCategory::Custom};
        bool loaded{false};     // false if disabled or load failed
    };

    static PluginManager &instance();

    // Scan a directory for .so/.dll files and load each as a plugin.
    // category controls whether disabled-state is checked (Custom only).
    // Also merges any scripts exported via getScriptsRegister() into ScriptsRegister::instance().
    void loadPluginsFromDirectory(const std::filesystem::path &pluginsDir,
                                  PluginCategory category = PluginCategory::Custom);

    void unloadAll();

    const std::vector<IPlugin *> &getLoadedPlugins() const;

    // Returns metadata for every plugin file discovered (even disabled ones).
    const std::vector<PluginInfo> &getPluginInfos() const;

private:
    struct PluginEntry
    {
        std::string filename; // basename, for duplicate detection
        LibraryHandle handle{nullptr};
        IPlugin *plugin{nullptr};              // nullptr if no createPlugin symbol
        void (*destroyer)(IPlugin *){nullptr}; // nullptr if no destroyPlugin symbol
        PluginCategory category{PluginCategory::Custom};
    };

    std::vector<PluginEntry> m_plugins;
    std::vector<IPlugin *> m_pluginPtrs;  // kept in sync for getLoadedPlugins()
    std::vector<PluginInfo> m_pluginInfos; // all discovered plugins (including disabled)
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PLUGIN_MANAGER_HPP
