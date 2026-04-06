#ifndef ELIX_PLUGINS_WINDOW_HPP
#define ELIX_PLUGINS_WINDOW_HPP

#include "Core/Macros.hpp"
#include "Editor/Panels/PluginDownloader.hpp"
#include "Engine/PluginSystem/PluginManager.hpp"

#include <string>
#include <unordered_set>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class PluginsWindow
{
public:
    void refresh();
    void draw(bool *open);

private:
    // ── Installed tab ─────────────────────────────────────────────────────────
    struct PluginDisplayEntry
    {
        std::string name;
        std::string filename;
        bool isEngine;
        bool isEnabled;
        bool pendingChange;
    };

    std::vector<PluginDisplayEntry> m_entries;
    bool m_restartNeeded{false};

    // ── Marketplace tab ───────────────────────────────────────────────────────
    PluginDownloader m_downloader;

    // Stems of plugins whose .so is already present in the plugins directory
    std::unordered_set<std::string> m_installedStems;

    void refreshInstalledStems();
    void drawMarketplaceTab();
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PLUGINS_WINDOW_HPP
