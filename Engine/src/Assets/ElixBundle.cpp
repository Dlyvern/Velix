#include "Engine/Assets/ElixBundle.hpp"
#include "Engine/Assets/Compressor.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <thread>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

#pragma pack(push, 1)
struct BundleFileHeader
{
    char magic[4];
    uint32_t version;
    uint32_t flags; // bit 0 = encrypted, bit 1 = default-compressed
    uint32_t entryCount;
    uint64_t tocOffset;
    uint64_t tocSize;
    uint32_t keyId;
    uint32_t chunkSize;
    uint8_t reserved[16];
};
static_assert(sizeof(BundleFileHeader) == 56);

struct BundleDiskChunk
{
    uint64_t offset;
    uint32_t storedSize;
    uint32_t originalSize;
};
static_assert(sizeof(BundleDiskChunk) == 16);
#pragma pack(pop)

// ---- Helpers ----

static void writeU32(std::ostream &os, uint32_t v)
{
    os.write(reinterpret_cast<const char *>(&v), 4);
}
static void writeU64(std::ostream &os, uint64_t v)
{
    os.write(reinterpret_cast<const char *>(&v), 8);
}
static void writeStr(std::ostream &os, const std::string &s)
{
    uint32_t len = static_cast<uint32_t>(s.size());
    writeU32(os, len);
    os.write(s.data(), len);
}

static uint32_t readU32(std::istream &is)
{
    uint32_t v;
    is.read(reinterpret_cast<char *>(&v), 4);
    return v;
}
static uint64_t readU64(std::istream &is)
{
    uint64_t v;
    is.read(reinterpret_cast<char *>(&v), 8);
    return v;
}
static std::string readStr(std::istream &is)
{
    uint32_t len = readU32(is);
    if (len == 0)
        return {};
    std::string s(len, '\0');
    is.read(s.data(), len);
    return s;
}

void ElixBundleWriter::addFile(std::string_view path, std::span<const uint8_t> data, bool compress)
{
    m_files.push_back({std::string(path),
                       std::vector<uint8_t>(data.begin(), data.end()),
                       compress});
}

void ElixBundleWriter::addFile(std::string_view path, std::vector<uint8_t> data, bool compress)
{
    m_files.push_back({std::string(path), std::move(data), compress});
}

bool ElixBundleWriter::write(const std::filesystem::path &outPath, uint32_t keyId) const
{
    // Two-pass: first collect all chunk data, then write header + TOC + chunks.
    const uint32_t chunkSize = BUNDLE_DEFAULT_CHUNK_SIZE;

    struct StagedEntry
    {
        std::string path;
        uint64_t uncompressedSize{0};
        uint8_t flags{0};
        std::vector<std::vector<uint8_t>> chunks; // compressed/encrypted payloads
        std::vector<uint32_t> originalSizes;
    };

    std::vector<StagedEntry> staged;
    staged.reserve(m_files.size());

    for (const auto &f : m_files)
    {
        StagedEntry e;
        e.path = f.path;
        e.uncompressedSize = f.data.size();
        e.flags = 0;

        const uint8_t *src = f.data.data();
        const size_t srcSize = f.data.size();
        size_t offset = 0;

        while (offset < srcSize || srcSize == 0)
        {
            const size_t blockSize = std::min(static_cast<size_t>(chunkSize), srcSize - offset);
            std::vector<uint8_t> raw(src + offset, src + offset + blockSize);

            if (f.compress)
            {
                std::vector<uint8_t> compressed;
                if (Compressor::compress(raw, compressed, Compressor::Algorithm::LZ4) &&
                    compressed.size() < raw.size())
                {
                    e.originalSizes.push_back(static_cast<uint32_t>(raw.size()));
                    e.chunks.push_back(std::move(compressed));
                    e.flags |= 0x01; // compressed
                }
                else
                {
                    e.originalSizes.push_back(static_cast<uint32_t>(raw.size()));
                    e.chunks.push_back(std::move(raw));
                }
            }
            else
            {
                e.originalSizes.push_back(static_cast<uint32_t>(raw.size()));
                e.chunks.push_back(std::move(raw));
            }

            offset += blockSize;
            if (srcSize == 0)
                break;
        }

        staged.push_back(std::move(e));
    }

    std::ofstream out(outPath, std::ios::binary);
    if (!out)
        return false;

    // Reserve header space (filled in later).
    BundleFileHeader hdr{};
    std::memcpy(hdr.magic, BUNDLE_MAGIC.data(), 4);
    hdr.version = BUNDLE_VERSION;
    hdr.flags = keyId ? 1u : 0u;
    hdr.entryCount = static_cast<uint32_t>(staged.size());
    hdr.keyId = keyId;
    hdr.chunkSize = chunkSize;
    out.write(reinterpret_cast<const char *>(&hdr), sizeof(hdr));

    // ---- TOC ----
    // Layout: [per-entry metadata] [string pool] [chunk index]
    // We build the TOC in a buffer so we can calculate its size before placing chunks.

    std::ostringstream tocBuf;

    // Per-entry metadata (fixed-size fields only, paths via string pool)
    // We'll write string pool separately; store path offsets after we know them.
    // Simpler: write TOC as: for each entry: pathLen(u32) + path + dataOffset(u64) + uncompressedSize(u64) + storedSize(u64) + chunkCount(u32) + firstChunkIndex(u32) + flags(u8) + pad(3)

    uint32_t totalChunks = 0;
    for (const auto &e : staged)
        totalChunks += static_cast<uint32_t>(e.chunks.size());

    for (const auto &e : staged)
    {
        writeStr(tocBuf, e.path);
        writeU64(tocBuf, 0ull); // dataOffset — placeholder, filled below
        writeU64(tocBuf, e.uncompressedSize);
        uint64_t storedSize = 0;
        for (const auto &ch : e.chunks)
            storedSize += ch.size();
        writeU64(tocBuf, storedSize);
        writeU32(tocBuf, static_cast<uint32_t>(e.chunks.size()));
        writeU32(tocBuf, 0u); // firstChunkIndex — filled below
        tocBuf.put(static_cast<char>(e.flags));
        tocBuf.put(0);
        tocBuf.put(0);
        tocBuf.put(0); // pad
    }

    // Chunk index entries
    for (const auto &e : staged)
    {
        for (size_t ci = 0; ci < e.chunks.size(); ++ci)
        {
            writeU64(tocBuf, 0ull); // offset placeholder
            writeU32(tocBuf, static_cast<uint32_t>(e.chunks[ci].size()));
            writeU32(tocBuf, e.originalSizes[ci]);
        }
    }

    const std::string tocStr = tocBuf.str();
    const uint64_t tocOffset = sizeof(BundleFileHeader);

    const uint64_t dataStart = tocOffset + tocStr.size();

    // Recompute TOC with correct offsets.
    std::ostringstream tocFinal;
    uint64_t currentOffset = dataStart;
    uint32_t chunkIndexBase = 0;

    for (const auto &e : staged)
    {
        writeStr(tocFinal, e.path);
        writeU64(tocFinal, currentOffset); // dataOffset
        writeU64(tocFinal, e.uncompressedSize);
        uint64_t storedSize = 0;
        for (const auto &ch : e.chunks)
            storedSize += ch.size();
        writeU64(tocFinal, storedSize);
        writeU32(tocFinal, static_cast<uint32_t>(e.chunks.size()));
        writeU32(tocFinal, chunkIndexBase);
        tocFinal.put(static_cast<char>(e.flags));
        tocFinal.put(0);
        tocFinal.put(0);
        tocFinal.put(0);

        for (const auto &ch : e.chunks)
            currentOffset += ch.size();
        chunkIndexBase += static_cast<uint32_t>(e.chunks.size());
    }

    // Chunk index with real offsets
    currentOffset = dataStart;
    for (const auto &e : staged)
    {
        for (size_t ci = 0; ci < e.chunks.size(); ++ci)
        {
            writeU64(tocFinal, currentOffset);
            writeU32(tocFinal, static_cast<uint32_t>(e.chunks[ci].size()));
            writeU32(tocFinal, e.originalSizes[ci]);
            currentOffset += e.chunks[ci].size();
        }
    }

    const std::string finalToc = tocFinal.str();

    // Patch header with correct TOC offset + size
    hdr.tocOffset = tocOffset;
    hdr.tocSize = static_cast<uint64_t>(finalToc.size());
    out.seekp(0);
    out.write(reinterpret_cast<const char *>(&hdr), sizeof(hdr));

    // Write TOC
    out.write(finalToc.data(), static_cast<std::streamsize>(finalToc.size()));

    // Write chunk data
    for (const auto &e : staged)
        for (const auto &ch : e.chunks)
            out.write(reinterpret_cast<const char *>(ch.data()), static_cast<std::streamsize>(ch.size()));

    return out.good();
}

bool ElixBundleWriter::writeProject(const std::filesystem::path &projectRoot,
                                    const std::filesystem::path &entryScenePath,
                                    const std::filesystem::path &outputBundlePath,
                                    const ExportOptions &options,
                                    std::string *errorMessage)
{
    auto setErr = [&](const std::string &msg) -> bool
    {
        if (errorMessage)
            *errorMessage = msg;
        return false;
    };

    std::error_code ec;
    const auto normRoot = std::filesystem::canonical(projectRoot, ec);
    if (ec)
        return setErr("Project root invalid: " + projectRoot.string());

    const auto normScene = std::filesystem::canonical(entryScenePath, ec);
    if (ec)
        return setErr("Entry scene not found: " + entryScenePath.string());

    // Compute relative entry scene path.
    const auto relScene = std::filesystem::relative(normScene, normRoot, ec);
    if (ec)
        return setErr("Entry scene is not inside project root.");

    // Normalise excluded directories.
    std::vector<std::filesystem::path> excluded;
    for (const auto &d : options.excludedDirectories)
    {
        if (d.empty())
            continue;
        std::error_code e2;
        auto norm = std::filesystem::canonical(d, e2);
        if (!e2)
            excluded.push_back(norm);
    }

    // Add a tiny manifest entry so the runtime can find the entry scene without extraction.
    const std::string manifestContent = relScene.string();
    addFile("__manifest__", std::vector<uint8_t>(manifestContent.begin(), manifestContent.end()), false);

    // Traverse project directory.
    std::filesystem::recursive_directory_iterator iter(normRoot,
                                                       std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec)
        return setErr("Failed to enumerate project: " + ec.message());

    for (auto it = iter; it != std::filesystem::recursive_directory_iterator(); ++it)
    {
        std::error_code e2;
        const auto abs = std::filesystem::canonical(it->path(), e2);
        if (e2 || !std::filesystem::is_regular_file(abs))
            continue;

        bool skip = false;
        for (const auto &excl : excluded)
        {
            auto rel = std::filesystem::relative(abs, excl, e2);
            if (!e2 && !rel.empty() && rel.native()[0] != '.')
            {
                skip = true;
                break;
            }
        }
        if (skip)
            continue;

        // Read file bytes.
        std::ifstream f(abs, std::ios::binary);
        if (!f)
            continue;
        const auto sz = std::filesystem::file_size(abs, e2);
        std::vector<uint8_t> data(sz);
        f.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(sz));

        const auto relPath = std::filesystem::relative(abs, normRoot, e2);
        addFile(relPath.string(), std::move(data), options.preferCompression);
    }

    return write(outputBundlePath);
}

ElixBundleReader::ElixBundleReader() = default;
ElixBundleReader::~ElixBundleReader() { unmount(); }

bool ElixBundleReader::mount(const std::filesystem::path &path)
{
    unmount();
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;

    BundleFileHeader hdr{};
    in.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));
    if (!in)
        return false;

    if (std::memcmp(hdr.magic, BUNDLE_MAGIC.data(), 4) != 0)
        return false;
    if (hdr.version != BUNDLE_VERSION)
        return false;

    m_path = path;
    m_encrypted = (hdr.flags & 0x01) != 0;
    m_keyId = hdr.keyId;
    m_chunkSize = hdr.chunkSize ? hdr.chunkSize : BUNDLE_DEFAULT_CHUNK_SIZE;

    // Read TOC
    in.seekg(static_cast<std::streamoff>(hdr.tocOffset));
    if (!in)
        return false;

    const uint32_t entryCount = hdr.entryCount;
    m_toc.resize(entryCount);

    // Also count total chunks (read entries first to know firstChunkIndex ranges)
    uint32_t totalChunks = 0;
    struct RawEntry
    {
        std::string path;
        uint64_t dataOffset, uncompressedSize, storedSize;
        uint32_t chunkCount, firstChunkIndex;
        uint8_t flags;
    };
    std::vector<RawEntry> raw(entryCount);

    for (uint32_t i = 0; i < entryCount; ++i)
    {
        raw[i].path = readStr(in);
        raw[i].dataOffset = readU64(in);
        raw[i].uncompressedSize = readU64(in);
        raw[i].storedSize = readU64(in);
        raw[i].chunkCount = readU32(in);
        raw[i].firstChunkIndex = readU32(in);
        raw[i].flags = static_cast<uint8_t>(in.get());
        in.get();
        in.get();
        in.get(); // pad
        totalChunks = std::max(totalChunks, raw[i].firstChunkIndex + raw[i].chunkCount);
    }

    // Read chunk index
    m_chunks.resize(totalChunks);
    for (uint32_t c = 0; c < totalChunks; ++c)
    {
        m_chunks[c].offset = readU64(in);
        m_chunks[c].storedSize = readU32(in);
        m_chunks[c].originalSize = readU32(in);
    }

    // Populate TOC
    for (uint32_t i = 0; i < entryCount; ++i)
    {
        auto &e = m_toc[i];
        e.path = raw[i].path;
        e.dataOffset = raw[i].dataOffset;
        e.uncompressedSize = raw[i].uncompressedSize;
        e.storedSize = raw[i].storedSize;
        e.chunkCount = raw[i].chunkCount;
        e.firstChunkIndex = raw[i].firstChunkIndex;
        e.flags = raw[i].flags;
        m_index[e.path] = i;
    }

    m_mounted = in.good();
    return m_mounted;
}

void ElixBundleReader::unmount()
{
    m_toc.clear();
    m_chunks.clear();
    m_index.clear();
    m_mounted = false;
    m_encrypted = false;
}

bool ElixBundleReader::contains(std::string_view path) const
{
    return m_index.count(std::string(path)) > 0;
}

const BundleTOCEntry *ElixBundleReader::findEntry(std::string_view path) const
{
    auto it = m_index.find(std::string(path));
    if (it == m_index.end())
        return nullptr;
    return &m_toc[it->second];
}

bool ElixBundleReader::readFile(std::string_view path, std::vector<uint8_t> &outData) const
{
    const BundleTOCEntry *entry = findEntry(path);
    if (!entry)
        return false;
    return readChunks(*entry, outData);
}

void ElixBundleReader::readFileAsync(std::string_view path,
                                     std::function<void(std::vector<uint8_t>)> callback) const
{
    std::string pathStr(path);
    // Post to streaming worker via AssetManager's worker (reuse same thread).
    // For simplicity, use a detached thread. A full impl would use AssetStreamingWorker.
    std::thread([this, pathStr, cb = std::move(callback)]()
                {
        std::vector<uint8_t> data;
        readFile(pathStr, data);
        cb(std::move(data)); })
        .detach();
}

bool ElixBundleReader::readChunks(const BundleTOCEntry &entry, std::vector<uint8_t> &out) const
{
    if (entry.chunkCount == 0)
    {
        out.clear();
        return true;
    }

    std::ifstream in(m_path, std::ios::binary);
    if (!in)
        return false;

    const bool compressed = (entry.flags & 0x01) != 0;
    out.reserve(static_cast<size_t>(entry.uncompressedSize));

    for (uint32_t ci = 0; ci < entry.chunkCount; ++ci)
    {
        const auto &chunkMeta = m_chunks[entry.firstChunkIndex + ci];

        in.seekg(static_cast<std::streamoff>(chunkMeta.offset));
        std::vector<uint8_t> raw(chunkMeta.storedSize);
        in.read(reinterpret_cast<char *>(raw.data()), chunkMeta.storedSize);
        if (!in)
            return false;

        if (compressed && chunkMeta.storedSize != chunkMeta.originalSize)
        {
            std::vector<uint8_t> decompressed;
            if (!Compressor::decompress(raw, chunkMeta.originalSize, decompressed, Compressor::Algorithm::LZ4))
                return false;
            out.insert(out.end(), decompressed.begin(), decompressed.end());
        }
        else
        {
            out.insert(out.end(), raw.begin(), raw.end());
        }
    }

    return true;
}

ElixBundleManager &ElixBundleManager::getInstance()
{
    static ElixBundleManager instance;
    return instance;
}

void ElixBundleManager::mountBundle(const std::filesystem::path &path, int priority)
{
    auto reader = std::make_unique<ElixBundleReader>();
    reader->priority = priority;
    if (!reader->mount(path))
        return;

    m_readers.push_back(std::move(reader));
    // Sort descending by priority so highest priority is searched first.
    std::sort(m_readers.begin(), m_readers.end(),
              [](const auto &a, const auto &b)
              { return a->priority > b->priority; });
}

void ElixBundleManager::unmountAll()
{
    m_readers.clear();
}

bool ElixBundleManager::contains(std::string_view path) const
{
    for (const auto &r : m_readers)
        if (r->contains(path))
            return true;
    return false;
}

bool ElixBundleManager::readFile(std::string_view path, std::vector<uint8_t> &outData) const
{
    for (const auto &r : m_readers)
        if (r->readFile(path, outData))
            return true;
    return false;
}

void ElixBundleManager::readFileAsync(std::string_view path,
                                      std::function<void(std::vector<uint8_t>)> callback) const
{
    // Try each bundle in priority order on a detached thread.
    std::string pathStr(path);
    std::thread([this, pathStr, cb = std::move(callback)]()
                {
        std::vector<uint8_t> data;
        for (const auto &r : m_readers)
            if (r->readFile(pathStr, data))
                break;
        cb(std::move(data)); })
        .detach();
}

ELIX_NESTED_NAMESPACE_END
