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
        Engine,
        Custom
    };

    enum class PluginLoadStatus
    {
        Loaded,
        Disabled,
        LibraryLoadFailed,
        MissingDependency,
        NoCreateSymbol,
    };

    struct PluginInfo
    {
        std::string pluginName;
        std::string filename;
        std::vector<std::string> missingDependencies;
        PluginCategory category{PluginCategory::Custom};
        PluginLoadStatus status{PluginLoadStatus::Loaded};
        bool loaded{false};
    };

    static PluginManager &instance();




    void loadPluginsFromDirectory(const std::filesystem::path &pluginsDir,
                                  PluginCategory category = PluginCategory::Custom);

    void unloadAll();

    const std::vector<IPlugin *> &getLoadedPlugins() const;


    const std::vector<PluginInfo> &getPluginInfos() const;

private:
    struct PluginEntry
    {
        std::string filename;
        LibraryHandle handle{nullptr};
        IPlugin *plugin{nullptr};
        void (*destroyer)(IPlugin *){nullptr};
        PluginCategory category{PluginCategory::Custom};
    };

    std::vector<PluginEntry> m_plugins;
    std::vector<IPlugin *> m_pluginPtrs;
    std::vector<PluginInfo> m_pluginInfos;
};

ELIX_NESTED_NAMESPACE_END

#endif
