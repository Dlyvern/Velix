#include "Engine/Assets/AssetsLoader.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
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
        std::unordered_set<std::string> extensions{".png"};
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
            << "  --ext <extension>     Additional extension to convert (repeatable).\n"
            << "                        Default: .png\n"
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

    uint32_t importedCount = 0u;
    uint32_t skippedCount = 0u;
    uint32_t failedCount = 0u;
    uint32_t deletedCount = 0u;

    std::cout << "Converting " << sourceFiles.size() << " texture(s)...\n";

    for (const auto &sourceFilePath : sourceFiles)
    {
        const std::filesystem::path outputPath = resolveOutputPath(options, absoluteInputPath, sourceFilePath);

        std::error_code parentDirectoryError;
        const auto outputParentDirectory = outputPath.parent_path();
        if (!outputParentDirectory.empty())
            std::filesystem::create_directories(outputParentDirectory, parentDirectoryError);

        if (parentDirectoryError)
        {
            std::cerr << "[FAILED] " << sourceFilePath << " -> " << outputPath << " (cannot create output directory)\n";
            ++failedCount;
            continue;
        }

        std::error_code outputExistsError;
        if (!options.overwrite && std::filesystem::exists(outputPath, outputExistsError) && !outputExistsError)
        {
            std::cout << "[SKIP]   " << sourceFilePath << " -> " << outputPath << " (already exists)\n";
            ++skippedCount;
            continue;
        }

        const bool imported = elix::engine::AssetsLoader::importTextureAsset(sourceFilePath.string(), outputPath.string());
        if (!imported)
        {
            std::cerr << "[FAILED] " << sourceFilePath << " -> " << outputPath << '\n';
            ++failedCount;
            continue;
        }

        ++importedCount;
        std::cout << "[OK]     " << sourceFilePath << " -> " << outputPath << '\n';

        if (options.deleteSource)
        {
            std::error_code removeError;
            std::filesystem::remove(sourceFilePath, removeError);
            if (!removeError)
                ++deletedCount;
            else
                std::cerr << "[WARN]   Imported but failed to delete source: " << sourceFilePath << '\n';
        }
    }

    std::cout << "\nSummary\n"
              << "  Imported: " << importedCount << '\n'
              << "  Skipped:  " << skippedCount << '\n'
              << "  Failed:   " << failedCount << '\n';

    if (options.deleteSource)
        std::cout << "  Deleted:  " << deletedCount << '\n';

    return failedCount == 0u ? 0 : 2;
}
