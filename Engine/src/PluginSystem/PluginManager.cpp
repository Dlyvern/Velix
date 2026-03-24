#include "Engine/PluginSystem/PluginManager.hpp"
#include "Engine/Scripting/ScriptsRegister.hpp"
#include "Core/Logger.hpp"

#include <algorithm>
#include <cctype>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace
{
    std::string sharedLibraryExtension()
    {
#if defined(_WIN32)
        return ".dll";
#elif defined(__linux__)
        return ".so";
#else
        return "";
#endif
    }

    bool isSharedLibrary(const std::filesystem::path &path)
    {
        const std::string ext = sharedLibraryExtension();
        if (ext.empty())
            return false;

#if defined(_WIN32)
        return path.extension() == ext;
#else
        const std::string filename = path.filename().string();
        return filename.find(ext) != std::string::npos;
#endif
    }

    bool isReservedLibraryName(const std::string &filename)
    {
        // Never load the SDK or game module as a plugin — they are managed separately.
        const std::vector<std::string> reserved = {"VelixSDK", "GameModule", "libGameModule"};
        for (const auto &name : reserved)
            if (filename.find(name) != std::string::npos)
                return true;
        return false;
    }
} // namespace

PluginManager &PluginManager::instance()
{
    static PluginManager s_instance;
    return s_instance;
}

void PluginManager::loadPluginsFromDirectory(const std::filesystem::path &pluginsDir)
{
    if (pluginsDir.empty() || !std::filesystem::exists(pluginsDir) || !std::filesystem::is_directory(pluginsDir))
        return;

    std::vector<std::filesystem::path> candidates;
    for (const auto &entry : std::filesystem::directory_iterator(pluginsDir))
    {
        if (!entry.is_regular_file())
            continue;

        const std::filesystem::path path = entry.path();
        if (!isSharedLibrary(path))
            continue;

        const std::string filename = path.filename().string();
        if (isReservedLibraryName(filename))
            continue;

        // Skip already-loaded filenames (project tier wins by loading last, same name skipped)
        const bool alreadyLoaded = std::any_of(m_plugins.begin(), m_plugins.end(),
                                               [&filename](const PluginEntry &e)
                                               { return e.filename == filename; });
        if (alreadyLoaded)
        {
            VX_ENGINE_INFO_STREAM("[PluginManager] Skipping already-loaded plugin: " << filename << '\n');
            continue;
        }

        candidates.push_back(path);
    }

    std::sort(candidates.begin(), candidates.end());

    for (const auto &path : candidates)
    {
        const std::string filename = path.filename().string();

        LibraryHandle handle = PluginLoader::loadLibrary(path.string());
        if (!handle)
        {
            VX_ENGINE_WARNING_STREAM("[PluginManager] Failed to load plugin library: " << path << '\n');
            continue;
        }

        // Merge any scripts the plugin exports into the main ScriptsRegister.
        using GetScriptsRegisterFn = ScriptsRegister &(*)();
        auto getRegisterFn = PluginLoader::getFunction<GetScriptsRegisterFn>("getScriptsRegister", handle);
        if (getRegisterFn)
        {
            ScriptsRegister &pluginRegister = getRegisterFn();
            ScriptsRegister &mainRegister = ScriptsRegister::instance();
            for (const auto &[name, factory] : pluginRegister.getScripts())
                mainRegister.registerScript(name, factory);

            VX_ENGINE_INFO_STREAM("[PluginManager] Merged " << pluginRegister.getScripts().size()
                                                            << " script(s) from plugin: " << filename << '\n');
        }

        // Lifecycle hooks (optional).
        using CreateFn = IPlugin *(*)();
        using DestroyFn = void (*)(IPlugin *);

        auto createFn = PluginLoader::getFunction<CreateFn>("createPlugin", handle);
        auto destroyFn = PluginLoader::getFunction<DestroyFn>("destroyPlugin", handle);

        IPlugin *plugin = nullptr;
        if (createFn)
        {
            plugin = createFn();
            if (plugin)
            {
                plugin->onLoad();
                VX_ENGINE_INFO_STREAM("[PluginManager] Loaded plugin: " << plugin->getName()
                                                                        << " v" << plugin->getVersion() << '\n');
                m_pluginPtrs.push_back(plugin);
            }
        }

        m_plugins.push_back({filename, handle, plugin, destroyFn});
    }
}

void PluginManager::unloadAll()
{
    // Unload in reverse load order.
    for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it)
    {
        PluginEntry &entry = *it;

        if (entry.plugin)
        {
            entry.plugin->onUnload();
            if (entry.destroyer)
                entry.destroyer(entry.plugin);
            entry.plugin = nullptr;
        }

        if (entry.handle)
        {
            PluginLoader::closeLibrary(entry.handle);
            entry.handle = nullptr;
        }
    }

    m_plugins.clear();
    m_pluginPtrs.clear();
}

const std::vector<IPlugin *> &PluginManager::getLoadedPlugins() const
{
    return m_pluginPtrs;
}

ELIX_NESTED_NAMESPACE_END
