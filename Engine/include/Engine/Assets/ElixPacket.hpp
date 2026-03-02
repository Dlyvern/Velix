#ifndef ELIX_PACKET_HPP
#define ELIX_PACKET_HPP

#include "Core/Macros.hpp"
#include "Engine/Assets/Compressor.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct ElixPacketFileEntry
{
    std::string relativePath;
    uint64_t uncompressedSize{0u};
    uint64_t storedSize{0u};
    Compressor::Algorithm compression{Compressor::Algorithm::None};
};

struct ElixPacketManifest
{
    std::string projectName;
    std::string entrySceneRelativePath;
    std::vector<ElixPacketFileEntry> files;
};

class ElixPacketSerializer
{
public:
    struct ExportOptions
    {
        std::vector<std::filesystem::path> excludedDirectories;
        bool preferCompression{true};
        int compressionLevel{6};
    };

    bool writeProject(const std::filesystem::path &projectRoot,
                      const std::filesystem::path &entryScenePath,
                      const std::filesystem::path &outputPacketPath,
                      const ExportOptions &options,
                      std::string *errorMessage = nullptr) const;

    bool writeProject(const std::filesystem::path &projectRoot,
                      const std::filesystem::path &entryScenePath,
                      const std::filesystem::path &outputPacketPath,
                      std::string *errorMessage = nullptr) const;
};

class ElixPacketDeserializer
{
public:
    bool readManifest(const std::filesystem::path &packetPath,
                      ElixPacketManifest &outManifest,
                      std::string *errorMessage = nullptr) const;

    bool extractToDirectory(const std::filesystem::path &packetPath,
                            const std::filesystem::path &outputDirectory,
                            ElixPacketManifest *outManifest = nullptr,
                            std::string *errorMessage = nullptr) const;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PACKET_HPP
