#ifndef ELIX_BUNDLE_HPP
#define ELIX_BUNDLE_HPP

#include "Core/Macros.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

static constexpr std::array<char, 4> BUNDLE_MAGIC = {'E', 'L', 'X', 'B'};
static constexpr uint32_t BUNDLE_VERSION = 1u;
static constexpr uint32_t BUNDLE_DEFAULT_CHUNK_SIZE = 65536u;

struct BundleTOCEntry
{
    std::string path;
    uint64_t dataOffset{0};
    uint64_t uncompressedSize{0};
    uint64_t storedSize{0};
    uint32_t chunkCount{0};
    uint32_t firstChunkIndex{0};
    uint8_t flags{0};
};

struct BundleChunkEntry
{
    uint64_t offset{0};
    uint32_t storedSize{0};
    uint32_t originalSize{0};
};

struct BundleExportOptions
{
    std::vector<std::filesystem::path> excludedDirectories;
    bool preferCompression{true};
    std::string entrySceneRelativePath;
};

class ElixBundleWriter
{
public:
    struct FileEntry
    {
        std::string path;
        std::vector<uint8_t> data;
        bool compress{true};
    };


    using ExportOptions = BundleExportOptions;

    void addFile(std::string_view path, std::span<const uint8_t> data, bool compress = true);
    void addFile(std::string_view path, std::vector<uint8_t> data, bool compress = true);



    bool write(const std::filesystem::path &outPath, uint32_t keyId = 0) const;


    bool writeProject(const std::filesystem::path &projectRoot,
                      const std::filesystem::path &entryScenePath,
                      const std::filesystem::path &outputBundlePath,
                      const BundleExportOptions &options = {},
                      std::string *errorMessage = nullptr);

    void clear() { m_files.clear(); }

private:
    std::vector<FileEntry> m_files;
};

class ElixBundleReader
{
public:
    ElixBundleReader();
    ~ElixBundleReader();

    ElixBundleReader(const ElixBundleReader &) = delete;
    ElixBundleReader &operator=(const ElixBundleReader &) = delete;

    bool mount(const std::filesystem::path &path);
    void unmount();
    bool isMounted() const { return m_mounted; }

    bool contains(std::string_view path) const;


    bool readFile(std::string_view path, std::vector<uint8_t> &outData) const;


    void readFileAsync(std::string_view path,
                       std::function<void(std::vector<uint8_t>)> callback) const;

    const BundleTOCEntry *findEntry(std::string_view path) const;

    int priority{0};

private:
    bool readChunks(const BundleTOCEntry &entry, std::vector<uint8_t> &out) const;

    std::filesystem::path m_path;
    std::vector<BundleTOCEntry> m_toc;
    std::vector<BundleChunkEntry> m_chunks;
    std::unordered_map<std::string, uint32_t> m_index;
    bool m_mounted{false};
    bool m_encrypted{false};
    uint32_t m_keyId{0};
    uint32_t m_chunkSize{BUNDLE_DEFAULT_CHUNK_SIZE};
};

class ElixBundleManager
{
public:
    static ElixBundleManager &getInstance();

    ElixBundleManager(const ElixBundleManager &) = delete;
    ElixBundleManager &operator=(const ElixBundleManager &) = delete;


    void mountBundle(const std::filesystem::path &path, int priority = 0);
    void unmountAll();


    bool readFile(std::string_view path, std::vector<uint8_t> &outData) const;


    void readFileAsync(std::string_view path,
                       std::function<void(std::vector<uint8_t>)> callback) const;

    bool contains(std::string_view path) const;

private:
    ElixBundleManager() = default;

    std::vector<std::unique_ptr<ElixBundleReader>> m_readers;
};

ELIX_NESTED_NAMESPACE_END

#endif
