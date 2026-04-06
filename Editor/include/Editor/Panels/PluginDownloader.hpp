#ifndef ELIX_PLUGIN_DOWNLOADER_HPP
#define ELIX_PLUGIN_DOWNLOADER_HPP

#include "Core/Macros.hpp"

#include <atomic>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

struct MarketplaceEntry
{
    std::string name;
    std::string version;
    std::string description;
    std::string category;
    std::string downloadUrl; // platform-appropriate URL resolved at parse time
};

class PluginDownloader
{
public:
    static constexpr const char *kManifestUrl =
        "https://raw.githubusercontent.com/Dlyvern/EnginePlugins/main/manifest.json";

    void fetchManifest();

    void poll();

    bool isFetching() const { return m_fetching; }
    bool isManifestReady() const { return m_manifestReady; }
    bool hasFetchError() const { return m_fetchError; }
    const std::string &fetchError() const { return m_fetchErrorMsg; }

    const std::vector<MarketplaceEntry> &getEntries() const { return m_entries; }

    void downloadPlugin(const MarketplaceEntry &entry,
                        const std::filesystem::path &destDir);

    bool isDownloading() const { return m_downloading; }
    bool isDownloadDone() const { return m_downloadDone; }
    bool hasDownloadError() const { return m_downloadError; }
    float downloadProgress() const { return m_downloadProgress.load(); }
    const std::string &downloadingName() const { return m_downloadingName; }
    const std::string &downloadError() const { return m_downloadErrorMsg; }

    void resetDownload();

private:
    static std::string httpGet(const std::string &url);
    static bool httpDownloadFile(const std::string &url,
                                 const std::filesystem::path &destPath,
                                 std::atomic<float> &outProgress);
    static std::vector<MarketplaceEntry> parseManifest(const std::string &json);

    std::future<std::string> m_fetchFuture;
    bool m_fetching{false};
    bool m_manifestReady{false};
    bool m_fetchError{false};
    std::string m_fetchErrorMsg;
    std::vector<MarketplaceEntry> m_entries;

    std::future<bool> m_downloadFuture;
    bool m_downloading{false};
    bool m_downloadDone{false};
    bool m_downloadError{false};
    std::string m_downloadErrorMsg;
    std::string m_downloadingName;
    std::atomic<float> m_downloadProgress{0.0f};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PLUGIN_DOWNLOADER_HPP
