#include "Engine/Assets/AssetsLoader.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace
{
    struct Options
    {
        std::filesystem::path inputPath;
        std::optional<std::filesystem::path> outputDirectory;
        bool recursive{false};
        bool deleteSource{false};
        bool overwrite{false};
        uint32_t jobs{0u};
        uint32_t maxTextureSize{2048u};
        std::unordered_set<std::string> extensions{".png", ".jpg", ".dds", ".tga", ".TGA"};
    };

    enum class ImportStatus : uint8_t
    {
        Imported,
        Skipped,
        Failed
    };

    struct ImportResult
    {
        std::filesystem::path sourcePath;
        std::filesystem::path outputPath;
        ImportStatus status{ImportStatus::Failed};
        const char *reason{nullptr};
        bool deletedSource{false};
        bool deleteFailed{false};
    };

    std::string toLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    void printUsage(const char *executableName)
    {
        std::cout
            << "Velix Texture Importer\n"
            << "Converts source textures into .tex.elixasset.\n\n"
            << "Usage:\n"
            << "  " << executableName << " --input <path> [options]\n"
            << "  " << executableName << " <path> [options]\n\n"
            << "Options:\n"
            << "  --output-dir <path>   Output directory for converted assets.\n"
            << "                        If omitted, writes next to each source file.\n"
            << "  --recursive           Recurse when input is a directory.\n"
            << "  --delete-source       Delete source texture after successful conversion.\n"
            << "  --overwrite           Overwrite existing .tex.elixasset files.\n"
            << "  --jobs <count>        Worker threads for conversion (0 = auto).\n"
            << "                        Default: 0\n"
            << "  --max-size <pixels>   Clamp imported texture dimensions (0 = keep original).\n"
            << "                        Default: 2048\n"
            << "  --ext <extension>     Additional extension to convert (repeatable).\n"
            << "                        Default: .png, .jpg\n"
            << "  --all-textures        Include common image extensions.\n"
            << "  --help                Show this help.\n\n"
            << "Examples:\n"
            << "  " << executableName << " ./resources/textures --recursive --delete-source\n"
            << "  " << executableName << " --input ./raw --output-dir ./resources/textures --recursive --all-textures\n";
    }

    bool parseArguments(int argc, char **argv, Options &outOptions)
    {
        std::vector<std::string> positionalArguments;

        for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
        {
            const std::string argument = argv[argumentIndex];

            if (argument == "--input")
            {
                if (argumentIndex + 1 >= argc)
                {
                    std::cerr << "Missing value for --input\n";
                    return false;
                }

                outOptions.inputPath = argv[++argumentIndex];
                continue;
            }

            if (argument == "--output-dir")
            {
                if (argumentIndex + 1 >= argc)
                {
                    std::cerr << "Missing value for --output-dir\n";
                    return false;
                }

                outOptions.outputDirectory = std::filesystem::path(argv[++argumentIndex]);
                continue;
            }

            if (argument == "--recursive")
            {
                outOptions.recursive = true;
                continue;
            }

            if (argument == "--delete-source")
            {
                outOptions.deleteSource = true;
                continue;
            }

            if (argument == "--overwrite")
            {
                outOptions.overwrite = true;
                continue;
            }

            if (argument == "--jobs")
            {
                if (argumentIndex + 1 >= argc)
                {
                    std::cerr << "Missing value for --jobs\n";
                    return false;
                }

                const std::string value = argv[++argumentIndex];
                char *endPointer = nullptr;
                const unsigned long parsed = std::strtoul(value.c_str(), &endPointer, 10);
                if (!endPointer || endPointer == value.c_str())
                {
                    std::cerr << "Invalid value for --jobs: " << value << '\n';
                    return false;
                }

                const unsigned long clamped =
                    std::min(parsed, static_cast<unsigned long>(std::numeric_limits<uint32_t>::max()));
                outOptions.jobs = static_cast<uint32_t>(clamped);
                continue;
            }

            if (argument == "--max-size")
            {
                if (argumentIndex + 1 >= argc)
                {
                    std::cerr << "Missing value for --max-size\n";
                    return false;
                }

                const std::string value = argv[++argumentIndex];
                char *endPointer = nullptr;
                const unsigned long parsed = std::strtoul(value.c_str(), &endPointer, 10);
                if (!endPointer || endPointer == value.c_str())
                {
                    std::cerr << "Invalid value for --max-size: " << value << '\n';
                    return false;
                }

                const unsigned long clamped =
                    std::min(parsed, static_cast<unsigned long>(std::numeric_limits<uint32_t>::max()));
                outOptions.maxTextureSize = static_cast<uint32_t>(clamped);
                continue;
            }

            if (argument == "--ext")
            {
                if (argumentIndex + 1 >= argc)
                {
                    std::cerr << "Missing value for --ext\n";
                    return false;
                }

                std::string extension = toLowerCopy(argv[++argumentIndex]);
                if (!extension.empty() && extension[0] != '.')
                    extension = "." + extension;

                if (!extension.empty())
                    outOptions.extensions.insert(extension);

                continue;
            }

            if (argument == "--all-textures")
            {
                static const std::array<const char *, 11> textureExtensions = {
                    ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".tiff", ".psd", ".gif", ".hdr", ".exr", ".dds"};

                outOptions.extensions.clear();
                for (const char *extension : textureExtensions)
                    outOptions.extensions.insert(extension);
                continue;
            }

            if (!argument.empty() && argument[0] == '-')
            {
                std::cerr << "Unknown option: " << argument << '\n';
                return false;
            }

            positionalArguments.push_back(argument);
        }

        if (outOptions.inputPath.empty())
        {
            if (positionalArguments.empty())
            {
                std::cerr << "Input path is required.\n";
                return false;
            }

            outOptions.inputPath = positionalArguments.front();
        }

        return true;
    }

    bool isSupportedSource(const std::filesystem::path &path, const std::unordered_set<std::string> &extensions)
    {
        const std::string extension = toLowerCopy(path.extension().string());
        return extensions.find(extension) != extensions.end();
    }

    std::vector<std::filesystem::path> gatherSourceFiles(const Options &options, const std::filesystem::path &absoluteInputPath)
    {
        std::vector<std::filesystem::path> files;

        std::error_code errorCode;
        if (std::filesystem::is_regular_file(absoluteInputPath, errorCode) && !errorCode)
        {
            if (isSupportedSource(absoluteInputPath, options.extensions))
                files.push_back(absoluteInputPath.lexically_normal());
            return files;
        }

        errorCode.clear();
        if (!std::filesystem::is_directory(absoluteInputPath, errorCode) || errorCode)
            return files;

        if (options.recursive)
        {
            for (std::filesystem::recursive_directory_iterator iterator(absoluteInputPath, errorCode);
                 !errorCode && iterator != std::filesystem::recursive_directory_iterator();
                 iterator.increment(errorCode))
            {
                const auto &entry = *iterator;
                std::error_code fileError;
                if (!entry.is_regular_file(fileError) || fileError)
                    continue;

                const auto path = entry.path().lexically_normal();
                if (isSupportedSource(path, options.extensions))
                    files.push_back(path);
            }
        }
        else
        {
            for (std::filesystem::directory_iterator iterator(absoluteInputPath, errorCode);
                 !errorCode && iterator != std::filesystem::directory_iterator();
                 iterator.increment(errorCode))
            {
                const auto &entry = *iterator;
                std::error_code fileError;
                if (!entry.is_regular_file(fileError) || fileError)
                    continue;

                const auto path = entry.path().lexically_normal();
                if (isSupportedSource(path, options.extensions))
                    files.push_back(path);
            }
        }

        std::sort(files.begin(), files.end(), [](const std::filesystem::path &left, const std::filesystem::path &right)
                  { return left.string() < right.string(); });

        return files;
    }

    uint32_t resolveJobCount(uint32_t requestedJobs, size_t sourceFileCount)
    {
        if (sourceFileCount == 0u)
            return 0u;

        size_t jobs = requestedJobs;
        if (jobs == 0u)
        {
            jobs = static_cast<size_t>(std::thread::hardware_concurrency());
            if (jobs == 0u)
                jobs = 1u;
        }

        jobs = std::max<size_t>(1u, std::min(jobs, sourceFileCount));
        return static_cast<uint32_t>(jobs);
    }

    std::filesystem::path resolveOutputPath(const Options &options,
                                            const std::filesystem::path &absoluteInputPath,
                                            const std::filesystem::path &sourceFilePath)
    {
        if (!options.outputDirectory.has_value())
        {
            auto outputPath = sourceFilePath;
            outputPath.replace_extension(".tex.elixasset");
            return outputPath.lexically_normal();
        }

        const std::filesystem::path outputRoot = std::filesystem::absolute(options.outputDirectory.value()).lexically_normal();

        if (std::filesystem::is_regular_file(absoluteInputPath))
        {
            const std::string outputFileName = sourceFilePath.stem().string() + ".tex.elixasset";
            return (outputRoot / outputFileName).lexically_normal();
        }

        std::error_code relativeError;
        const std::filesystem::path relativePath = std::filesystem::relative(sourceFilePath, absoluteInputPath, relativeError);

        auto outputPath = relativeError ? (outputRoot / sourceFilePath.filename()) : (outputRoot / relativePath);
        outputPath.replace_extension(".tex.elixasset");
        return outputPath.lexically_normal();
    }
} // namespace

int main(int argc, char **argv)
{
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const std::string argument = argv[argumentIndex];
        if (argument == "--help" || argument == "-h")
        {
            printUsage(argv[0]);
            return 0;
        }
    }

    Options options;
    if (!parseArguments(argc, argv, options))
        return 1;

    elix::engine::AssetsLoader::setTextureImportMaxDimension(options.maxTextureSize);

    const std::filesystem::path absoluteInputPath = std::filesystem::absolute(options.inputPath).lexically_normal();

    std::error_code inputExistsError;
    if (!std::filesystem::exists(absoluteInputPath, inputExistsError) || inputExistsError)
    {
        std::cerr << "Input path does not exist: " << absoluteInputPath << '\n';
        return 1;
    }

    if (options.outputDirectory.has_value())
    {
        const std::filesystem::path absoluteOutputDirectory = std::filesystem::absolute(options.outputDirectory.value()).lexically_normal();
        std::error_code outputError;
        std::filesystem::create_directories(absoluteOutputDirectory, outputError);
        if (outputError)
        {
            std::cerr << "Failed to create output directory: " << absoluteOutputDirectory << '\n';
            return 1;
        }
    }

    std::vector<std::filesystem::path> sourceFiles = gatherSourceFiles(options, absoluteInputPath);
    if (sourceFiles.empty())
    {
        std::cout << "No matching source textures found.\n";
        return 0;
    }
    const uint32_t jobCount = resolveJobCount(options.jobs, sourceFiles.size());
    std::cout << "Converting " << sourceFiles.size() << " texture(s) with " << jobCount << " worker(s)...\n";

    std::atomic_size_t nextFileIndex{0u};
    std::vector<ImportResult> results(sourceFiles.size());
    std::vector<std::thread> workers;
    workers.reserve(jobCount);

    const auto processNextFile = [&]()
    {
        while (true)
        {
            const size_t sourceIndex = nextFileIndex.fetch_add(1u, std::memory_order_relaxed);
            if (sourceIndex >= sourceFiles.size())
                return;

            ImportResult &result = results[sourceIndex];
            result.sourcePath = sourceFiles[sourceIndex];
            result.outputPath = resolveOutputPath(options, absoluteInputPath, result.sourcePath);

            std::error_code parentDirectoryError;
            const auto outputParentDirectory = result.outputPath.parent_path();
            if (!outputParentDirectory.empty())
                std::filesystem::create_directories(outputParentDirectory, parentDirectoryError);

            if (parentDirectoryError)
            {
                result.status = ImportStatus::Failed;
                result.reason = "cannot create output directory";
                continue;
            }

            std::error_code outputExistsError;
            if (!options.overwrite && std::filesystem::exists(result.outputPath, outputExistsError) && !outputExistsError)
            {
                result.status = ImportStatus::Skipped;
                result.reason = "already exists";
                continue;
            }

            const bool imported =
                elix::engine::AssetsLoader::importTextureAsset(result.sourcePath.string(), result.outputPath.string());
            if (!imported)
            {
                result.status = ImportStatus::Failed;
                result.reason = nullptr;
                continue;
            }

            result.status = ImportStatus::Imported;
            if (!options.deleteSource)
                continue;

            std::error_code removeError;
            std::filesystem::remove(result.sourcePath, removeError);
            if (!removeError)
                result.deletedSource = true;
            else
                result.deleteFailed = true;
        }
    };

    if (jobCount == 1u)
    {
        processNextFile();
    }
    else
    {
        for (uint32_t workerIndex = 0u; workerIndex < jobCount; ++workerIndex)
            workers.emplace_back(processNextFile);

        for (auto &worker : workers)
            worker.join();
    }

    uint32_t importedCount = 0u;
    uint32_t skippedCount = 0u;
    uint32_t failedCount = 0u;
    uint32_t deletedCount = 0u;

    for (const auto &result : results)
    {
        if (result.status == ImportStatus::Imported)
        {
            ++importedCount;
            std::cout << "[OK]     " << result.sourcePath << " -> " << result.outputPath << '\n';

            if (result.deletedSource)
                ++deletedCount;
            else if (result.deleteFailed)
                std::cerr << "[WARN]   Imported but failed to delete source: " << result.sourcePath << '\n';

            continue;
        }

        if (result.status == ImportStatus::Skipped)
        {
            ++skippedCount;
            std::cout << "[SKIP]   " << result.sourcePath << " -> " << result.outputPath;
            if (result.reason)
                std::cout << " (" << result.reason << ")";
            std::cout << '\n';
            continue;
        }

        ++failedCount;
        std::cerr << "[FAILED] " << result.sourcePath << " -> " << result.outputPath;
        if (result.reason)
            std::cerr << " (" << result.reason << ")";
        std::cerr << '\n';
    }

    std::cout << "\nSummary\n"
              << "  Imported: " << importedCount << '\n'
              << "  Skipped:  " << skippedCount << '\n'
              << "  Failed:   " << failedCount << '\n';

    if (options.deleteSource)
        std::cout << "  Deleted:  " << deletedCount << '\n';

    return failedCount == 0u ? 0 : 2;
}
