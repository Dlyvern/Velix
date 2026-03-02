#include "Engine/Assets/ElixPacket.hpp"

#include "Core/Logger.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace
{
    constexpr std::array<char, 4> PACKET_MAGIC{'E', 'L', 'X', 'P'};
    constexpr uint32_t PACKET_VERSION = 1u;
    constexpr uint32_t MAX_STRING_SIZE = 16u * 1024u * 1024u;
    constexpr uint64_t MAX_FILE_SIZE = 2ull * 1024ull * 1024ull * 1024ull;

    struct PacketHeader
    {
        char magic[4];
        uint32_t version;
        uint32_t fileCount;
        uint8_t reserved[8];
    };

    struct PacketEntryHeader
    {
        uint32_t pathSize;
        uint8_t compression;
        uint8_t reserved[7];
        uint64_t uncompressedSize;
        uint64_t storedSize;
    };

    template <typename T>
    bool writePOD(std::ostream &stream, const T &value)
    {
        stream.write(reinterpret_cast<const char *>(&value), static_cast<std::streamsize>(sizeof(T)));
        return stream.good();
    }

    template <typename T>
    bool readPOD(std::istream &stream, T &value)
    {
        stream.read(reinterpret_cast<char *>(&value), static_cast<std::streamsize>(sizeof(T)));
        return stream.good();
    }

    void setError(std::string *errorMessage, const std::string &message)
    {
        if (errorMessage)
            *errorMessage = message;
    }

    std::filesystem::path normalizeAbsolutePath(const std::filesystem::path &path)
    {
        std::error_code errorCode;
        const auto absolutePath = std::filesystem::absolute(path, errorCode);
        if (errorCode)
            return path.lexically_normal();

        return absolutePath.lexically_normal();
    }

    bool isPathWithinRoot(const std::filesystem::path &path, const std::filesystem::path &root)
    {
        if (root.empty())
            return false;

        const auto normalizedPath = normalizeAbsolutePath(path);
        const auto normalizedRoot = normalizeAbsolutePath(root);

        std::error_code relativeError;
        const auto relativePath = std::filesystem::relative(normalizedPath, normalizedRoot, relativeError).lexically_normal();
        if (relativeError || relativePath.empty())
            return false;

        if (relativePath == ".")
            return true;

        for (const auto &part : relativePath)
            if (part == "..")
                return false;

        return true;
    }

    bool isSafeRelativePath(const std::filesystem::path &path)
    {
        if (path.empty() || path.is_absolute() || path.has_root_path())
            return false;

        for (const auto &part : path)
            if (part == "..")
                return false;

        return true;
    }

    bool writeSizedString(std::ostream &stream, const std::string &text)
    {
        if (text.size() > std::numeric_limits<uint32_t>::max())
            return false;

        const uint32_t size = static_cast<uint32_t>(text.size());
        if (!writePOD(stream, size))
            return false;

        if (size == 0u)
            return true;

        stream.write(text.data(), static_cast<std::streamsize>(size));
        return stream.good();
    }

    bool readSizedString(std::istream &stream, std::string &outText)
    {
        uint32_t size = 0u;
        if (!readPOD(stream, size))
            return false;

        if (size > MAX_STRING_SIZE)
            return false;

        outText.resize(size);
        if (size == 0u)
            return true;

        stream.read(outText.data(), static_cast<std::streamsize>(size));
        return stream.good();
    }

    bool readBinaryFile(const std::filesystem::path &path, std::vector<uint8_t> &outBytes, std::string *errorMessage)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream.is_open())
        {
            setError(errorMessage, "Failed to open file: " + path.string());
            return false;
        }

        const std::streamsize size = stream.tellg();
        if (size < 0)
        {
            setError(errorMessage, "Failed to read file size: " + path.string());
            return false;
        }

        if (static_cast<uint64_t>(size) > MAX_FILE_SIZE)
        {
            setError(errorMessage, "File is too large for packet export: " + path.string());
            return false;
        }

        outBytes.resize(static_cast<size_t>(size));
        stream.seekg(0, std::ios::beg);
        if (!outBytes.empty())
            stream.read(reinterpret_cast<char *>(outBytes.data()), size);

        if (!stream.good() && !stream.eof())
        {
            setError(errorMessage, "Failed to read file bytes: " + path.string());
            return false;
        }

        return true;
    }

    bool writeBinaryFile(const std::filesystem::path &path, const std::vector<uint8_t> &bytes, std::string *errorMessage)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
        {
            setError(errorMessage, "Failed to open output file: " + path.string());
            return false;
        }

        if (!bytes.empty())
            stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

        if (!stream.good())
        {
            setError(errorMessage, "Failed to write output file: " + path.string());
            return false;
        }

        return true;
    }

    bool skipBytes(std::istream &stream, uint64_t count)
    {
        if (count == 0u)
            return true;

        if (count > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
            return false;

        stream.seekg(static_cast<std::streamoff>(count), std::ios::cur);
        return stream.good();
    }
} // namespace

ELIX_NESTED_NAMESPACE_BEGIN(engine)

bool ElixPacketSerializer::writeProject(const std::filesystem::path &projectRoot,
                                        const std::filesystem::path &entryScenePath,
                                        const std::filesystem::path &outputPacketPath,
                                        const ExportOptions &options,
                                        std::string *errorMessage) const
{
    const auto normalizedProjectRoot = normalizeAbsolutePath(projectRoot);
    if (!std::filesystem::exists(normalizedProjectRoot) || !std::filesystem::is_directory(normalizedProjectRoot))
    {
        setError(errorMessage, "Project root is invalid: " + normalizedProjectRoot.string());
        return false;
    }

    const auto normalizedEntryScenePath = normalizeAbsolutePath(entryScenePath);
    if (!std::filesystem::exists(normalizedEntryScenePath) || !std::filesystem::is_regular_file(normalizedEntryScenePath))
    {
        setError(errorMessage, "Entry scene file was not found: " + normalizedEntryScenePath.string());
        return false;
    }

    if (!isPathWithinRoot(normalizedEntryScenePath, normalizedProjectRoot))
    {
        std::ostringstream message;
        message << "Entry scene must be inside the project root. "
                << "entryScene='" << normalizedEntryScenePath.string() << "', "
                << "projectRoot='" << normalizedProjectRoot.string() << "'";
        setError(errorMessage, message.str());
        return false;
    }

    std::error_code relativeError;
    const auto relativeEntryScenePath = std::filesystem::relative(normalizedEntryScenePath, normalizedProjectRoot, relativeError).lexically_normal();
    if (relativeError || !isSafeRelativePath(relativeEntryScenePath))
    {
        setError(errorMessage, "Failed to compute relative entry scene path.");
        return false;
    }

    auto normalizedOutputPacketPath = normalizeAbsolutePath(outputPacketPath);
    const auto outputDirectory = normalizedOutputPacketPath.parent_path();
    std::error_code directoryError;
    std::filesystem::create_directories(outputDirectory, directoryError);
    if (directoryError)
    {
        setError(errorMessage, "Failed to create packet output directory: " + outputDirectory.string());
        return false;
    }

    std::vector<std::filesystem::path> excludedDirectories;
    excludedDirectories.reserve(options.excludedDirectories.size());
    for (const auto &excludedDirectory : options.excludedDirectories)
    {
        if (excludedDirectory.empty())
            continue;

        excludedDirectories.push_back(normalizeAbsolutePath(excludedDirectory));
    }

    std::vector<std::filesystem::path> filesToPackage;
    std::error_code iteratorError;
    std::filesystem::recursive_directory_iterator iterator(
        normalizedProjectRoot,
        std::filesystem::directory_options::skip_permission_denied,
        iteratorError);

    if (iteratorError)
    {
        setError(errorMessage, "Failed to enumerate project files: " + iteratorError.message());
        return false;
    }

    for (auto it = iterator; it != std::filesystem::recursive_directory_iterator(); ++it)
    {
        const auto currentPath = normalizeAbsolutePath(it->path());

        bool excluded = false;
        for (const auto &excludedDirectory : excludedDirectories)
        {
            if (isPathWithinRoot(currentPath, excludedDirectory))
            {
                excluded = true;
                break;
            }
        }

        if (excluded)
        {
            if (it->is_directory())
                it.disable_recursion_pending();

            continue;
        }

        if (!it->is_regular_file())
            continue;

        if (currentPath == normalizedOutputPacketPath)
            continue;

        filesToPackage.push_back(currentPath);
    }

    std::sort(filesToPackage.begin(), filesToPackage.end(), [](const auto &a, const auto &b)
              { return a.generic_string() < b.generic_string(); });

    if (filesToPackage.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
    {
        setError(errorMessage, "Too many files to package.");
        return false;
    }

    std::ofstream output(normalizedOutputPacketPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        setError(errorMessage, "Failed to open packet output file: " + normalizedOutputPacketPath.string());
        return false;
    }

    PacketHeader header{};
    std::memcpy(header.magic, PACKET_MAGIC.data(), PACKET_MAGIC.size());
    header.version = PACKET_VERSION;
    header.fileCount = static_cast<uint32_t>(filesToPackage.size());
    std::memset(header.reserved, 0, sizeof(header.reserved));

    if (!writePOD(output, header))
    {
        setError(errorMessage, "Failed to write packet header.");
        return false;
    }

    const std::string projectName = normalizedProjectRoot.filename().string();
    if (!writeSizedString(output, projectName) ||
        !writeSizedString(output, relativeEntryScenePath.generic_string()))
    {
        setError(errorMessage, "Failed to write packet metadata.");
        return false;
    }

    for (const auto &filePath : filesToPackage)
    {
        std::vector<uint8_t> sourceBytes;
        if (!readBinaryFile(filePath, sourceBytes, errorMessage))
            return false;

        Compressor::Algorithm algorithm = Compressor::Algorithm::None;
        std::vector<uint8_t> storedBytes = sourceBytes;
        if (options.preferCompression && !sourceBytes.empty())
        {
            std::vector<uint8_t> compressedBytes;
            if (Compressor::compress(sourceBytes, compressedBytes, Compressor::Algorithm::Deflate, options.compressionLevel) &&
                compressedBytes.size() < sourceBytes.size())
            {
                algorithm = Compressor::Algorithm::Deflate;
                storedBytes = std::move(compressedBytes);
            }
        }

        std::error_code relativeFileError;
        const auto relativeFilePath = std::filesystem::relative(filePath, normalizedProjectRoot, relativeFileError).lexically_normal();
        if (relativeFileError || !isSafeRelativePath(relativeFilePath))
        {
            setError(errorMessage, "Failed to compute packet relative path for file: " + filePath.string());
            return false;
        }

        const std::string relativeFilePathString = relativeFilePath.generic_string();

        PacketEntryHeader entryHeader{};
        entryHeader.pathSize = static_cast<uint32_t>(relativeFilePathString.size());
        entryHeader.compression = static_cast<uint8_t>(algorithm);
        std::memset(entryHeader.reserved, 0, sizeof(entryHeader.reserved));
        entryHeader.uncompressedSize = static_cast<uint64_t>(sourceBytes.size());
        entryHeader.storedSize = static_cast<uint64_t>(storedBytes.size());

        if (!writePOD(output, entryHeader))
        {
            setError(errorMessage, "Failed to write packet entry header: " + relativeFilePathString);
            return false;
        }

        if (entryHeader.pathSize > 0u)
            output.write(relativeFilePathString.data(), static_cast<std::streamsize>(entryHeader.pathSize));

        if (!output.good())
        {
            setError(errorMessage, "Failed to write packet entry path: " + relativeFilePathString);
            return false;
        }

        if (!storedBytes.empty())
            output.write(reinterpret_cast<const char *>(storedBytes.data()), static_cast<std::streamsize>(storedBytes.size()));

        if (!output.good())
        {
            setError(errorMessage, "Failed to write packet entry bytes: " + relativeFilePathString);
            return false;
        }
    }

    if (!output.good())
    {
        setError(errorMessage, "Packet write failed.");
        return false;
    }

    return true;
}

bool ElixPacketSerializer::writeProject(const std::filesystem::path &projectRoot,
                                        const std::filesystem::path &entryScenePath,
                                        const std::filesystem::path &outputPacketPath,
                                        std::string *errorMessage) const
{
    const ExportOptions options{};
    return writeProject(projectRoot, entryScenePath, outputPacketPath, options, errorMessage);
}

bool ElixPacketDeserializer::readManifest(const std::filesystem::path &packetPath,
                                          ElixPacketManifest &outManifest,
                                          std::string *errorMessage) const
{
    outManifest = {};

    std::ifstream input(packetPath, std::ios::binary);
    if (!input.is_open())
    {
        setError(errorMessage, "Failed to open packet: " + packetPath.string());
        return false;
    }

    PacketHeader header{};
    if (!readPOD(input, header))
    {
        setError(errorMessage, "Failed to read packet header.");
        return false;
    }

    if (std::memcmp(header.magic, PACKET_MAGIC.data(), PACKET_MAGIC.size()) != 0)
    {
        setError(errorMessage, "Invalid packet magic.");
        return false;
    }

    if (header.version != PACKET_VERSION)
    {
        setError(errorMessage, "Unsupported packet version.");
        return false;
    }

    if (!readSizedString(input, outManifest.projectName) ||
        !readSizedString(input, outManifest.entrySceneRelativePath))
    {
        setError(errorMessage, "Failed to read packet metadata.");
        return false;
    }

    outManifest.files.reserve(header.fileCount);

    for (uint32_t fileIndex = 0u; fileIndex < header.fileCount; ++fileIndex)
    {
        PacketEntryHeader entryHeader{};
        if (!readPOD(input, entryHeader))
        {
            setError(errorMessage, "Failed to read packet entry header.");
            return false;
        }

        if (entryHeader.pathSize > MAX_STRING_SIZE)
        {
            setError(errorMessage, "Packet entry path is too long.");
            return false;
        }

        std::string relativePath(entryHeader.pathSize, '\0');
        if (entryHeader.pathSize > 0u)
            input.read(relativePath.data(), static_cast<std::streamsize>(entryHeader.pathSize));

        if (!input.good())
        {
            setError(errorMessage, "Failed to read packet entry path.");
            return false;
        }

        ElixPacketFileEntry entry{};
        entry.relativePath = std::move(relativePath);
        entry.uncompressedSize = entryHeader.uncompressedSize;
        entry.storedSize = entryHeader.storedSize;
        entry.compression = static_cast<Compressor::Algorithm>(entryHeader.compression);
        outManifest.files.push_back(std::move(entry));

        if (!skipBytes(input, entryHeader.storedSize))
        {
            setError(errorMessage, "Failed to skip packet entry payload.");
            return false;
        }
    }

    return true;
}

bool ElixPacketDeserializer::extractToDirectory(const std::filesystem::path &packetPath,
                                                const std::filesystem::path &outputDirectory,
                                                ElixPacketManifest *outManifest,
                                                std::string *errorMessage) const
{
    std::ifstream input(packetPath, std::ios::binary);
    if (!input.is_open())
    {
        setError(errorMessage, "Failed to open packet: " + packetPath.string());
        return false;
    }

    PacketHeader header{};
    if (!readPOD(input, header))
    {
        setError(errorMessage, "Failed to read packet header.");
        return false;
    }

    if (std::memcmp(header.magic, PACKET_MAGIC.data(), PACKET_MAGIC.size()) != 0)
    {
        setError(errorMessage, "Invalid packet magic.");
        return false;
    }

    if (header.version != PACKET_VERSION)
    {
        setError(errorMessage, "Unsupported packet version.");
        return false;
    }

    ElixPacketManifest manifest{};
    if (!readSizedString(input, manifest.projectName) ||
        !readSizedString(input, manifest.entrySceneRelativePath))
    {
        setError(errorMessage, "Failed to read packet metadata.");
        return false;
    }

    std::error_code createDirectoryError;
    std::filesystem::create_directories(outputDirectory, createDirectoryError);
    if (createDirectoryError)
    {
        setError(errorMessage, "Failed to create extraction output directory: " + outputDirectory.string());
        return false;
    }

    manifest.files.reserve(header.fileCount);

    for (uint32_t fileIndex = 0u; fileIndex < header.fileCount; ++fileIndex)
    {
        PacketEntryHeader entryHeader{};
        if (!readPOD(input, entryHeader))
        {
            setError(errorMessage, "Failed to read packet entry header.");
            return false;
        }

        if (entryHeader.pathSize > MAX_STRING_SIZE)
        {
            setError(errorMessage, "Packet entry path is too long.");
            return false;
        }

        std::string relativePath(entryHeader.pathSize, '\0');
        if (entryHeader.pathSize > 0u)
            input.read(relativePath.data(), static_cast<std::streamsize>(entryHeader.pathSize));

        if (!input.good())
        {
            setError(errorMessage, "Failed to read packet entry path.");
            return false;
        }

        const std::filesystem::path relativeFilePath(relativePath);
        if (!isSafeRelativePath(relativeFilePath))
        {
            setError(errorMessage, "Unsafe packet entry path: " + relativePath);
            return false;
        }

        if (entryHeader.storedSize > MAX_FILE_SIZE)
        {
            setError(errorMessage, "Packet entry is too large: " + relativePath);
            return false;
        }

        std::vector<uint8_t> storedBytes(static_cast<size_t>(entryHeader.storedSize));
        if (!storedBytes.empty())
            input.read(reinterpret_cast<char *>(storedBytes.data()), static_cast<std::streamsize>(storedBytes.size()));

        if (!input.good())
        {
            setError(errorMessage, "Failed to read packet entry bytes: " + relativePath);
            return false;
        }

        std::vector<uint8_t> outputBytes;
        const auto algorithm = static_cast<Compressor::Algorithm>(entryHeader.compression);
        if (!Compressor::decompress(storedBytes, static_cast<size_t>(entryHeader.uncompressedSize), outputBytes, algorithm))
        {
            setError(errorMessage, "Failed to decompress packet entry: " + relativePath);
            return false;
        }

        const auto targetPath = (outputDirectory / relativeFilePath).lexically_normal();
        std::error_code parentDirectoryError;
        std::filesystem::create_directories(targetPath.parent_path(), parentDirectoryError);
        if (parentDirectoryError)
        {
            setError(errorMessage, "Failed to create packet extraction directory: " + targetPath.parent_path().string());
            return false;
        }

        if (!writeBinaryFile(targetPath, outputBytes, errorMessage))
            return false;

        ElixPacketFileEntry manifestEntry{};
        manifestEntry.relativePath = relativePath;
        manifestEntry.uncompressedSize = entryHeader.uncompressedSize;
        manifestEntry.storedSize = entryHeader.storedSize;
        manifestEntry.compression = algorithm;
        manifest.files.push_back(std::move(manifestEntry));
    }

    if (outManifest)
        *outManifest = std::move(manifest);

    return true;
}

ELIX_NESTED_NAMESPACE_END
