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
static constexpr uint32_t BUNDLE_DEFAULT_CHUNK_SIZE = 65536u; // 64 KB

struct BundleTOCEntry
{
    std::string path;
    uint64_t dataOffset{0}; // byte offset to first chunk
    uint64_t uncompressedSize{0};
    uint64_t storedSize{0}; // sum of all chunk storedSizes
    uint32_t chunkCount{0};
    uint32_t firstChunkIndex{0}; // index into flat chunk array
    uint8_t flags{0};            // bit 0 = compressed, bit 1 = encrypted
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
    std::string entrySceneRelativePath; // written to "__manifest__" entry
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

    // Kept for backward-compat — callers may use ElixBundleWriter::ExportOptions
    using ExportOptions = BundleExportOptions;

    void addFile(std::string_view path, std::span<const uint8_t> data, bool compress = true);
    void addFile(std::string_view path, std::vector<uint8_t> data, bool compress = true);

    // Serialize all staged files to an .elixbundle file.
    // keyId == 0 → no encryption.
    bool write(const std::filesystem::path &outPath, uint32_t keyId = 0) const;

    // High-level: traverse projectRoot, add all files, write bundle.
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

    // Synchronous: reads, decompresses, and returns file bytes.
    bool readFile(std::string_view path, std::vector<uint8_t> &outData) const;

    // Async: posts to AssetStreamingWorker, calls callback on worker thread.
    void readFileAsync(std::string_view path,
                       std::function<void(std::vector<uint8_t>)> callback) const;

    const BundleTOCEntry *findEntry(std::string_view path) const;

    int priority{0};

private:
    bool readChunks(const BundleTOCEntry &entry, std::vector<uint8_t> &out) const;

    std::filesystem::path m_path;
    std::vector<BundleTOCEntry> m_toc;
    std::vector<BundleChunkEntry> m_chunks;
    std::unordered_map<std::string, uint32_t> m_index; // path → toc index
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

    // Mount a bundle. Higher priority wins on path collision.
    void mountBundle(const std::filesystem::path &path, int priority = 0);
    void unmountAll();

    // Returns true and fills outData if any mounted bundle contains the path.
    bool readFile(std::string_view path, std::vector<uint8_t> &outData) const;

    // Async variant — callback called on streaming worker thread.
    void readFileAsync(std::string_view path,
                       std::function<void(std::vector<uint8_t>)> callback) const;

    bool contains(std::string_view path) const;

private:
    ElixBundleManager() = default;

    std::vector<std::unique_ptr<ElixBundleReader>> m_readers; // sorted by priority desc
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_BUNDLE_HPP
