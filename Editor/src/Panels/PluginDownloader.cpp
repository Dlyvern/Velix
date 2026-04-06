#include "Editor/Panels/PluginDownloader.hpp"

#include <curl/curl.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

// ── Simple JSON helpers (no external dependency) ─────────────────────────────

static std::string jsonStringValue(const std::string &json, const std::string &key)
{
    // Finds  "key": "value"  in a flat JSON object.
    const std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos)
        return {};
    pos = json.find('"', pos + needle.size());
    if (pos == std::string::npos)
        return {};
    ++pos; // skip opening quote
    auto end = json.find('"', pos);
    if (end == std::string::npos)
        return {};
    return json.substr(pos, end - pos);
}

// ── libcurl write callbacks ───────────────────────────────────────────────────

static size_t writeToString(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *str = static_cast<std::string *>(userdata);
    str->append(ptr, size * nmemb);
    return size * nmemb;
}

static size_t writeToFile(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *ofs = static_cast<std::ofstream *>(userdata);
    ofs->write(ptr, static_cast<std::streamsize>(size * nmemb));
    return size * nmemb;
}

struct DownloadProgressCtx
{
    std::atomic<float> *progress{nullptr};
};

static int progressCallback(void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t /*ultotal*/, curl_off_t /*ulnow*/)
{
    if (dltotal > 0)
    {
        auto *ctx = static_cast<DownloadProgressCtx *>(userdata);
        ctx->progress->store(static_cast<float>(dlnow) / static_cast<float>(dltotal));
    }
    return 0; // non-zero would abort
}

// ── Static helpers ────────────────────────────────────────────────────────────

ELIX_NESTED_NAMESPACE_BEGIN(editor)

std::string PluginDownloader::httpGet(const std::string &url)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        throw std::runtime_error("curl_easy_init failed");

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw std::runtime_error(std::string("curl GET failed: ") + curl_easy_strerror(res));

    return body;
}

bool PluginDownloader::httpDownloadFile(const std::string &url,
                                        const std::filesystem::path &destPath,
                                        std::atomic<float> &outProgress)
{
    std::ofstream ofs(destPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open())
        return false;

    CURL *curl = curl_easy_init();
    if (!curl)
        return false;

    DownloadProgressCtx progressCtx{&outProgress};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ofs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressCtx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    ofs.close();

    if (res != CURLE_OK)
    {
        std::filesystem::remove(destPath); // clean up partial download
        return false;
    }

    outProgress.store(1.0f);
    return true;
}

std::vector<MarketplaceEntry> PluginDownloader::parseManifest(const std::string &json)
{
    std::vector<MarketplaceEntry> entries;

    // Walk through each { ... } object in the "plugins" array.
    std::size_t pos = 0;
    while ((pos = json.find('{', pos)) != std::string::npos)
    {
        auto end = json.find('}', pos);
        if (end == std::string::npos)
            break;

        const std::string obj = json.substr(pos, end - pos + 1);
        pos = end + 1;

        const std::string name = jsonStringValue(obj, "name");
        if (name.empty())
            continue; // skip the outer "{" wrapper

        MarketplaceEntry entry;
        entry.name        = name;
        entry.version     = jsonStringValue(obj, "version");
        entry.description = jsonStringValue(obj, "description");
        entry.category    = jsonStringValue(obj, "category");

#if defined(_WIN32)
        entry.downloadUrl = jsonStringValue(obj, "windows");
#else
        entry.downloadUrl = jsonStringValue(obj, "linux");
#endif

        entries.push_back(std::move(entry));
    }

    return entries;
}

// ── Public API ────────────────────────────────────────────────────────────────

void PluginDownloader::fetchManifest()
{
    if (m_fetching || m_manifestReady)
        return;

    m_fetching     = true;
    m_fetchError   = false;
    m_fetchErrorMsg.clear();
    m_manifestReady = false;
    m_entries.clear();

    m_fetchFuture = std::async(std::launch::async, []() -> std::string {
        return httpGet(kManifestUrl);
    });
}

void PluginDownloader::poll()
{
    // ── Manifest future ──────────────────────────────────────────────────────
    if (m_fetching && m_fetchFuture.valid())
    {
        if (m_fetchFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            try
            {
                const std::string body = m_fetchFuture.get();
                m_entries       = parseManifest(body);
                m_manifestReady = true;
            }
            catch (const std::exception &e)
            {
                m_fetchError    = true;
                m_fetchErrorMsg = e.what();
            }
            m_fetching = false;
        }
    }

    // ── Download future ──────────────────────────────────────────────────────
    if (m_downloading && m_downloadFuture.valid())
    {
        if (m_downloadFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            const bool ok    = m_downloadFuture.get();
            m_downloadDone   = ok;
            m_downloadError  = !ok;
            if (!ok)
                m_downloadErrorMsg = "Download failed — check internet connection or try again.";
            m_downloading = false;
        }
    }
}

void PluginDownloader::downloadPlugin(const MarketplaceEntry &entry,
                                      const std::filesystem::path &destDir)
{
    if (m_downloading)
        return;

    m_downloading      = true;
    m_downloadDone     = false;
    m_downloadError    = false;
    m_downloadErrorMsg.clear();
    m_downloadingName  = entry.name;
    m_downloadProgress.store(0.0f);

    const std::string url = entry.downloadUrl;
    const std::filesystem::path dest = destDir / (entry.name +
#if defined(_WIN32)
        ".dll"
#else
        ".so"
#endif
    );

    std::filesystem::create_directories(destDir);

    m_downloadFuture = std::async(std::launch::async,
        [url, dest, &progress = m_downloadProgress]() -> bool {
            return httpDownloadFile(url, dest, progress);
        });
}

void PluginDownloader::resetDownload()
{
    m_downloading      = false;
    m_downloadDone     = false;
    m_downloadError    = false;
    m_downloadErrorMsg.clear();
    m_downloadingName.clear();
    m_downloadProgress.store(0.0f);
}

ELIX_NESTED_NAMESPACE_END
