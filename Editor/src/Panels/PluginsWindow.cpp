#include "Editor/Panels/PluginsWindow.hpp"

#include "Engine/PluginSystem/PluginManager.hpp"
#include "Engine/Runtime/EngineConfig.hpp"

#include <imgui.h>

#include <algorithm>
#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

void PluginsWindow::refresh()
{
    m_entries.clear();
    m_restartNeeded = false;

    const auto &infos = engine::PluginManager::instance().getPluginInfos();
    const auto &config = engine::EngineConfig::instance();

    for (const auto &info : infos)
    {
        PluginDisplayEntry entry;
        entry.name = info.pluginName.empty() ? info.filename : info.pluginName;

        std::string stem = info.filename;
        const auto dotPos = stem.rfind('.');
        if (dotPos != std::string::npos)
            stem = stem.substr(0, dotPos);
        if (stem.substr(0, 3) == "lib")
            stem = stem.substr(3);

        entry.filename = stem;
        entry.isEngine = (info.category == engine::PluginManager::PluginCategory::Engine);
        entry.isEnabled = config.isPluginEnabled(stem);
        entry.pendingChange = false;
        m_entries.push_back(std::move(entry));
    }

    refreshInstalledStems();
}

void PluginsWindow::refreshInstalledStems()
{
    m_installedStems.clear();

    // Check which plugin .so files already exist next to the executable.
    namespace fs = std::filesystem;
    const fs::path pluginsDir = fs::canonical("/proc/self/exe").parent_path() / "resources" / "plugins";
    if (!fs::exists(pluginsDir))
        return;

    for (const auto &de : fs::directory_iterator(pluginsDir))
    {
        if (!de.is_regular_file())
            continue;
        std::string stem = de.path().stem().string();
        if (stem.substr(0, 3) == "lib")
            stem = stem.substr(3);
        m_installedStems.insert(stem);
    }
}

void PluginsWindow::drawMarketplaceTab()
{
    m_downloader.poll();

    const bool busy = m_downloader.isFetching() || m_downloader.isDownloading();

    if (busy)
        ImGui::BeginDisabled();

    if (ImGui::Button("Refresh"))
    {
        m_downloader.fetchManifest();
    }

    if (busy)
        ImGui::EndDisabled();

    if (m_downloader.isFetching())
    {
        ImGui::SameLine();
        ImGui::TextUnformatted("Fetching manifest...");
    }

    if (m_downloader.hasFetchError())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("Error: %s", m_downloader.fetchError().c_str());
        ImGui::PopStyleColor();
    }

    if (m_downloader.isDownloading())
    {
        ImGui::Separator();
        ImGui::Text("Downloading %s...", m_downloader.downloadingName().c_str());
        ImGui::ProgressBar(m_downloader.downloadProgress());
    }

    if (m_downloader.isDownloadDone())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.4f, 1.0f));
        ImGui::Text("%s downloaded. Restart to activate.", m_downloader.downloadingName().c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("OK"))
        {
            refreshInstalledStems();
            m_downloader.resetDownload();
            m_restartNeeded = true;
        }
    }

    if (m_downloader.hasDownloadError())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("Download error: %s", m_downloader.downloadError().c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss"))
            m_downloader.resetDownload();
    }

    ImGui::Separator();

    if (!m_downloader.isManifestReady())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::TextUnformatted("Press Refresh to load the plugin catalogue.");
        ImGui::PopStyleColor();
        return;
    }

    const auto &marketEntries = m_downloader.getEntries();
    if (marketEntries.empty())
    {
        ImGui::TextUnformatted("No plugins available in the marketplace.");
        return;
    }

    namespace fs = std::filesystem;
    const fs::path pluginsDir = fs::canonical("/proc/self/exe").parent_path() / "resources" / "plugins";

    for (const auto &me : marketEntries)
    {
        ImGui::PushID(me.name.c_str());

        const bool installed = m_installedStems.count(me.name) > 0;

        // Name + version
        ImGui::Text("%s  v%s", me.name.c_str(), me.version.c_str());

        // Description (smaller, grey)
        if (!me.description.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
            ImGui::TextWrapped("%s", me.description.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90.0f);

        if (installed)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
            ImGui::TextUnformatted("Installed");
            ImGui::PopStyleColor();
        }
        else
        {
            const bool downloading = m_downloader.isDownloading() && m_downloader.downloadingName() == me.name;
            if (downloading)
                ImGui::BeginDisabled();

            if (ImGui::Button("Install"))
                m_downloader.downloadPlugin(me, pluginsDir);

            if (downloading)
                ImGui::EndDisabled();
        }

        ImGui::Separator();
        ImGui::PopID();
    }
}

void PluginsWindow::draw(bool *open)
{
    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Plugins", open))
    {
        ImGui::End();
        return;
    }

    if (m_restartNeeded)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.1f, 1.0f));
        ImGui::TextWrapped("Restart required for plugin changes to take effect.");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    if (ImGui::BeginTabBar("PluginsTabs"))
    {
        if (ImGui::BeginTabItem("Installed"))
        {
            if (ImGui::CollapsingHeader("Engine Plugins", ImGuiTreeNodeFlags_DefaultOpen))
            {
                bool any = false;
                for (auto &entry : m_entries)
                {
                    if (!entry.isEngine)
                        continue;
                    any = true;

                    bool enabled = entry.isEnabled;
                    const bool pushedColor = entry.pendingChange;
                    if (pushedColor)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.1f, 1.0f));

                    if (ImGui::Checkbox(entry.name.c_str(), &enabled))
                    {
                        entry.isEnabled     = enabled;
                        entry.pendingChange = !entry.pendingChange;
                        engine::EngineConfig::instance().setPluginEnabled(entry.filename, enabled);
                        engine::EngineConfig::instance().save();
                        m_restartNeeded = true;
                    }

                    if (pushedColor)
                        ImGui::PopStyleColor();

                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", entry.filename.c_str());
                }
                if (!any)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    ImGui::TextUnformatted("No engine plugins loaded.");
                    ImGui::PopStyleColor();
                }
            }

            ImGui::Spacing();

            if (ImGui::CollapsingHeader("Custom Plugins", ImGuiTreeNodeFlags_DefaultOpen))
            {
                bool any = false;
                for (auto &entry : m_entries)
                {
                    if (entry.isEngine)
                        continue;
                    any = true;

                    bool enabled = entry.isEnabled;
                    const bool pushedColor = entry.pendingChange;
                    if (pushedColor)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.1f, 1.0f));

                    if (ImGui::Checkbox(entry.name.c_str(), &enabled))
                    {
                        entry.isEnabled     = enabled;
                        entry.pendingChange = !entry.pendingChange;
                        engine::EngineConfig::instance().setPluginEnabled(entry.filename, enabled);
                        engine::EngineConfig::instance().save();
                        m_restartNeeded = true;
                    }

                    if (pushedColor)
                        ImGui::PopStyleColor();

                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", entry.filename.c_str());
                }
                if (!any)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    ImGui::TextUnformatted("No custom plugins found.");
                    ImGui::PopStyleColor();
                }
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Marketplace"))
        {
            drawMarketplaceTab();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

ELIX_NESTED_NAMESPACE_END
