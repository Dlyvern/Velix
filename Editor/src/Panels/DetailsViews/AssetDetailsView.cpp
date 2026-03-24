#include "Editor/Panels/DetailsViews/AssetDetailsView.hpp"

#include "Editor/Editor.hpp"

#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Assets/AssetsSerializer.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Material.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

namespace
{
    std::string toLowerCopy(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return text;
    }

    std::string textureExtensionLower(const std::string &path)
    {
        return toLowerCopy(std::filesystem::path(path).extension().string());
    }

    bool isDataTextureUsage(TextureUsage usage)
    {
        return usage == TextureUsage::Data || usage == TextureUsage::PreviewData;
    }

    VkFormat getLdrTextureFormat(TextureUsage usage)
    {
        return isDataTextureUsage(usage) ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
    }

    bool isEditableTextPath(const std::filesystem::path &path)
    {
        static const std::unordered_set<std::string> editableExtensions = {
            ".vert", ".frag", ".comp", ".geom", ".tesc", ".tese", ".glsl", ".hlsl", ".fx",
            ".cpp", ".cxx", ".cc", ".c", ".hpp", ".hh", ".hxx", ".h", ".inl",
            ".json", ".scene", ".yaml", ".yml", ".ini", ".cfg", ".toml",
            ".txt", ".md", ".cmake", ".elixproject"};

        const std::string extension = toLowerCopy(path.extension().string());
        return editableExtensions.find(extension) != editableExtensions.end();
    }

    bool isModelAssetPath(const std::filesystem::path &path)
    {
        if (toLowerCopy(path.extension().string()) != ".elixasset")
            return false;

        engine::AssetsSerializer serializer;
        const auto header = serializer.readHeader(path.string());
        if (!header.has_value())
            return false;

        return static_cast<engine::Asset::AssetType>(header->type) == engine::Asset::AssetType::MODEL;
    }

    bool isTextureAssetPath(const std::filesystem::path &path)
    {
        if (toLowerCopy(path.extension().string()) != ".elixasset")
            return false;

        engine::AssetsSerializer serializer;
        const auto header = serializer.readHeader(path.string());
        if (!header.has_value())
            return false;

        return static_cast<engine::Asset::AssetType>(header->type) == engine::Asset::AssetType::TEXTURE;
    }

    bool isScriptAssetPath(const std::filesystem::path &path)
    {
        static const std::unordered_set<std::string> scriptExtensions = {
            ".cpp", ".cxx", ".cc", ".c", ".hpp", ".hh", ".hxx", ".h"};

        const std::string extension = toLowerCopy(path.extension().string());
        return scriptExtensions.find(extension) != scriptExtensions.end();
    }

    std::filesystem::path makeUniquePathWithExtension(const std::filesystem::path &directory,
                                                      const std::string &baseName,
                                                      const std::string &extension)
    {
        std::filesystem::path candidate = directory / (baseName + extension);
        if (!std::filesystem::exists(candidate))
            return candidate;

        uint32_t suffix = 1;
        while (true)
        {
            candidate = directory / (baseName + "_" + std::to_string(suffix) + extension);
            if (!std::filesystem::exists(candidate))
                return candidate;

            ++suffix;
        }
    }

    std::string sanitizeFileStem(std::string value)
    {
        for (char &character : value)
        {
            const bool isAlphaNumeric = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
            const bool isAllowedPunctuation = character == '_' || character == '-';

            if (!isAlphaNumeric && !isAllowedPunctuation)
                character = '_';
        }

        if (value.empty())
            return "Material";

        return value;
    }

    std::string buildMaterialSignature(const elix::engine::CPUMaterial &material)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(6);
        stream << material.name << '\n'
               << material.albedoTexture << '\n'
               << material.normalTexture << '\n'
               << material.ormTexture << '\n'
               << material.emissiveTexture << '\n'
               << material.baseColorFactor.r << ',' << material.baseColorFactor.g << ',' << material.baseColorFactor.b << ',' << material.baseColorFactor.a << '\n'
               << material.emissiveFactor.r << ',' << material.emissiveFactor.g << ',' << material.emissiveFactor.b << '\n'
               << material.metallicFactor << '\n'
               << material.roughnessFactor << '\n'
               << material.aoStrength << '\n'
               << material.normalScale << '\n'
               << material.alphaCutoff << '\n'
               << material.uvScale.x << ',' << material.uvScale.y << '\n'
               << material.uvOffset.x << ',' << material.uvOffset.y << '\n'
               << material.uvRotation << '\n'
               << material.flags;
        return stream.str();
    }

    std::filesystem::path makeAbsoluteNormalized(const std::filesystem::path &path)
    {
        std::error_code errorCode;
        std::filesystem::path absolutePath = std::filesystem::absolute(path, errorCode);
        if (errorCode)
            return path.lexically_normal();

        return absolutePath.lexically_normal();
    }

    bool isPathWithinRoot(const std::filesystem::path &path, const std::filesystem::path &root)
    {
        if (root.empty())
            return false;

        std::error_code errorCode;
        const std::filesystem::path normalizedPath = makeAbsoluteNormalized(path);
        const std::filesystem::path normalizedRoot = makeAbsoluteNormalized(root);
        const std::filesystem::path relativePath = std::filesystem::relative(normalizedPath, normalizedRoot, errorCode);

        if (errorCode || relativePath.empty())
            return false;

        const std::string relative = relativePath.lexically_normal().string();
        if (relative == ".")
            return true;

        return relative.rfind("..", 0) != 0;
    }

    std::string toProjectRelativePathIfPossible(const std::filesystem::path &path, const std::filesystem::path &projectRoot)
    {
        const std::filesystem::path normalizedPath = makeAbsoluteNormalized(path);
        if (projectRoot.empty() || !isPathWithinRoot(normalizedPath, projectRoot))
            return normalizedPath.string();

        std::error_code errorCode;
        const std::filesystem::path relativePath = std::filesystem::relative(normalizedPath, makeAbsoluteNormalized(projectRoot), errorCode);
        if (errorCode || relativePath.empty())
            return normalizedPath.string();

        return relativePath.lexically_normal().string();
    }

    bool tryResolveCandidateTexturePath(const std::filesystem::path &candidatePath,
                                        std::string &outResolvedPath)
    {
        std::error_code existsError;
        if (!std::filesystem::exists(candidatePath, existsError) || existsError)
            return false;

        std::error_code regularFileError;
        if (!std::filesystem::is_regular_file(candidatePath, regularFileError) || regularFileError)
            return false;

        outResolvedPath = makeAbsoluteNormalized(candidatePath).string();
        return true;
    }

    bool looksLikeWindowsAbsolutePath(const std::string &path)
    {
        return path.size() >= 3u &&
               std::isalpha(static_cast<unsigned char>(path[0])) &&
               path[1] == ':' &&
               (path[2] == '\\' || path[2] == '/');
    }

    std::string resolveTexturePathAgainstProjectRoot(const std::string &texturePath, const std::filesystem::path &projectRoot)
    {
        if (texturePath.empty())
            return {};

        if (looksLikeWindowsAbsolutePath(texturePath))
            return texturePath;

        const std::filesystem::path path(texturePath);
        if (path.is_absolute())
            return makeAbsoluteNormalized(path).string();

        if (projectRoot.empty())
            return texturePath;

        return makeAbsoluteNormalized(projectRoot / path).string();
    }

    std::string resolveMaterialPathAgainstProjectRoot(const std::string &materialPath, const std::filesystem::path &projectRoot)
    {
        if (materialPath.empty())
            return {};

        if (looksLikeWindowsAbsolutePath(materialPath))
            return materialPath;

        const std::filesystem::path parsedPath(materialPath);
        if (parsedPath.is_absolute())
            return makeAbsoluteNormalized(parsedPath).string();

        auto tryResolveExistingPath = [](const std::filesystem::path &candidatePath) -> std::optional<std::string>
        {
            if (candidatePath.empty())
                return std::nullopt;

            const std::filesystem::path normalizedPath = makeAbsoluteNormalized(candidatePath);
            std::error_code existsError;
            if (std::filesystem::exists(normalizedPath, existsError) && !existsError)
                return normalizedPath.string();

            return std::nullopt;
        };

        if (auto resolvedPath = tryResolveExistingPath(parsedPath); resolvedPath.has_value())
            return resolvedPath.value();

        if (!projectRoot.empty())
        {
            if (auto resolvedPath = tryResolveExistingPath(projectRoot / parsedPath); resolvedPath.has_value())
                return resolvedPath.value();

            const std::string projectRootName = toLowerCopy(projectRoot.filename().string());
            auto segmentIterator = parsedPath.begin();
            if (!projectRootName.empty() && segmentIterator != parsedPath.end() &&
                toLowerCopy(segmentIterator->string()) == projectRootName)
            {
                std::filesystem::path projectRelativePath;
                ++segmentIterator;
                for (; segmentIterator != parsedPath.end(); ++segmentIterator)
                    projectRelativePath /= *segmentIterator;

                if (auto resolvedPath = tryResolveExistingPath(projectRoot / projectRelativePath); resolvedPath.has_value())
                    return resolvedPath.value();
            }

            return makeAbsoluteNormalized(projectRoot / parsedPath).string();
        }

        return parsedPath.lexically_normal().string();
    }

    void sanitizeMaterialCpuData(engine::CPUMaterial &material, bool forceDielectricWithoutOrm)
    {
        auto sanitizeFinite = [](float value, float fallback) -> float
        {
            return std::isfinite(value) ? value : fallback;
        };

        material.metallicFactor = std::clamp(sanitizeFinite(material.metallicFactor, 0.0f), 0.0f, 1.0f);
        material.roughnessFactor = std::clamp(sanitizeFinite(material.roughnessFactor, 1.0f), 0.04f, 1.0f);
        material.aoStrength = std::clamp(sanitizeFinite(material.aoStrength, 1.0f), 0.0f, 1.0f);
        material.normalScale = std::max(0.0f, sanitizeFinite(material.normalScale, 1.0f));
        material.alphaCutoff = std::clamp(sanitizeFinite(material.alphaCutoff, 0.5f), 0.0f, 1.0f);
        material.ior = std::clamp(sanitizeFinite(material.ior, 1.5f), 1.0f, 2.6f);
        material.uvScale.x = sanitizeFinite(material.uvScale.x, 1.0f);
        material.uvScale.y = sanitizeFinite(material.uvScale.y, 1.0f);
        material.uvOffset.x = sanitizeFinite(material.uvOffset.x, 0.0f);
        material.uvOffset.y = sanitizeFinite(material.uvOffset.y, 0.0f);
        material.uvRotation = sanitizeFinite(material.uvRotation, 0.0f);

        const uint32_t legacyGlassFlag = engine::Material::MaterialFlags::EMATERIAL_FLAG_LEGACY_GLASS;
        if ((material.flags & legacyGlassFlag) != 0u)
        {
            material.flags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND;
            material.flags &= ~legacyGlassFlag;
        }

        const uint32_t supportedFlags =
            engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK |
            engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND |
            engine::Material::MaterialFlags::EMATERIAL_FLAG_DOUBLE_SIDED |
            engine::Material::MaterialFlags::EMATERIAL_FLAG_FLIP_V |
            engine::Material::MaterialFlags::EMATERIAL_FLAG_FLIP_U |
            engine::Material::MaterialFlags::EMATERIAL_FLAG_CLAMP_UV;
        material.flags &= supportedFlags;

        if (forceDielectricWithoutOrm && material.ormTexture.empty())
            material.metallicFactor = 0.0f;
    }

    std::optional<engine::Asset::AssetType> readSerializedAssetType(const std::filesystem::path &path)
    {
        if (toLowerCopy(path.extension().string()) != ".elixasset")
            return std::nullopt;

        engine::AssetsSerializer serializer;
        const auto header = serializer.readHeader(path.string());
        if (!header.has_value())
            return std::nullopt;

        return static_cast<engine::Asset::AssetType>(header->type);
    }

    std::string makeTextureAssetDisplayName(const std::string &texturePath)
    {
        if (texturePath.empty())
            return "<Default>";

        const std::string filename = std::filesystem::path(texturePath).filename().string();
        const std::string filenameLower = toLowerCopy(filename);

        constexpr const char *textureSuffix = ".tex.elixasset";
        constexpr const char *genericSuffix = ".elixasset";

        if (filenameLower.size() > std::strlen(textureSuffix) &&
            filenameLower.rfind(textureSuffix) == filenameLower.size() - std::strlen(textureSuffix))
            return filename.substr(0, filename.size() - std::strlen(textureSuffix));

        if (filenameLower.size() > std::strlen(genericSuffix) &&
            filenameLower.rfind(genericSuffix) == filenameLower.size() - std::strlen(genericSuffix))
            return filename.substr(0, filename.size() - std::strlen(genericSuffix));

        return filename;
    }

    std::filesystem::path buildProjectManagedTextureAssetPath(const std::filesystem::path &sourceTexturePath,
                                                              const std::filesystem::path &projectRoot,
                                                              const std::filesystem::path &preferredDirectory = {})
    {
        const std::filesystem::path normalizedSourcePath = makeAbsoluteNormalized(sourceTexturePath);
        if (!preferredDirectory.empty())
        {
            std::filesystem::path targetDirectory = preferredDirectory;
            if (targetDirectory.is_relative() && !projectRoot.empty())
                targetDirectory = makeAbsoluteNormalized(projectRoot / targetDirectory);
            else
                targetDirectory = makeAbsoluteNormalized(targetDirectory);

            std::string outputBaseName = sanitizeFileStem(normalizedSourcePath.stem().string());
            if (outputBaseName.empty())
                outputBaseName = "Texture";

            const uint64_t sourcePathHash = static_cast<uint64_t>(std::hash<std::string>{}(normalizedSourcePath.string()));
            const std::filesystem::path outputPath = targetDirectory / (outputBaseName + "_" + std::to_string(sourcePathHash) + ".tex.elixasset");
            return outputPath.lexically_normal();
        }

        if (projectRoot.empty())
        {
            auto outputPath = normalizedSourcePath;
            outputPath.replace_extension(".tex.elixasset");
            return outputPath.lexically_normal();
        }

        const std::filesystem::path normalizedProjectRoot = makeAbsoluteNormalized(projectRoot);
        if (isPathWithinRoot(normalizedSourcePath, normalizedProjectRoot))
        {
            std::error_code relativeError;
            const std::filesystem::path relativePath = std::filesystem::relative(normalizedSourcePath, normalizedProjectRoot, relativeError).lexically_normal();
            if (!relativeError && !relativePath.empty())
            {
                auto outputPath = normalizedProjectRoot / relativePath;
                outputPath.replace_extension(".tex.elixasset");
                return outputPath.lexically_normal();
            }
        }

        const std::filesystem::path importDirectory = (normalizedProjectRoot / "resources" / "textures" / "imported").lexically_normal();
        std::string outputBaseName = sanitizeFileStem(normalizedSourcePath.stem().string());
        if (outputBaseName.empty())
            outputBaseName = "Texture";

        const uint64_t sourcePathHash = static_cast<uint64_t>(std::hash<std::string>{}(normalizedSourcePath.string()));
        const std::filesystem::path outputPath = importDirectory / (outputBaseName + "_" + std::to_string(sourcePathHash) + ".tex.elixasset");
        return outputPath.lexically_normal();
    }

    std::string toMaterialTextureReferencePath(const std::string &texturePath,
                                               const std::filesystem::path &projectRoot,
                                               const std::filesystem::path &preferredImportDirectory = {},
                                               bool allowImportFromSource = true)
    {
        if (texturePath.empty())
            return {};

        std::filesystem::path resolvedPath = resolveTexturePathAgainstProjectRoot(texturePath, projectRoot);
        resolvedPath = resolvedPath.lexically_normal();

        std::string extensionLower = toLowerCopy(resolvedPath.extension().string());
        if (extensionLower != ".elixasset")
        {
            std::filesystem::path candidateAssetPath = buildProjectManagedTextureAssetPath(resolvedPath, projectRoot, preferredImportDirectory);

            std::error_code existsError;
            if (std::filesystem::exists(candidateAssetPath, existsError) && !existsError)
                resolvedPath = candidateAssetPath.lexically_normal();
            else if (allowImportFromSource)
            {
                std::error_code sourceExistsError;
                if (std::filesystem::exists(resolvedPath, sourceExistsError) && !sourceExistsError &&
                    engine::AssetsLoader::importTextureAsset(resolvedPath.string(), candidateAssetPath.string()))
                    resolvedPath = candidateAssetPath.lexically_normal();
            }
        }

        if (toLowerCopy(resolvedPath.extension().string()) == ".elixasset")
        {
            auto type = readSerializedAssetType(resolvedPath);
            if (type.has_value() && type.value() == engine::Asset::AssetType::TEXTURE)
                return toProjectRelativePathIfPossible(resolvedPath, projectRoot);
        }

        return toProjectRelativePathIfPossible(resolvedPath, projectRoot);
    }

    std::vector<std::string> gatherProjectTextureAssets(Project &project, const std::filesystem::path &projectRoot)
    {
        std::vector<std::string> texturePaths;
        std::unordered_set<std::string> uniquePaths;

        auto addTexturePath = [&](const std::filesystem::path &path)
        {
            const std::string normalized = toProjectRelativePathIfPossible(path.lexically_normal(), projectRoot);
            if (!uniquePaths.insert(normalized).second)
                return;

            auto &record = project.cache.texturesByPath[normalized];
            record.path = normalized;
            texturePaths.push_back(normalized);
        };

        for (const auto &[cachedPath, _] : project.cache.texturesByPath)
        {
            const std::string resolved = resolveTexturePathAgainstProjectRoot(cachedPath, projectRoot);
            const std::filesystem::path resolvedPath = std::filesystem::path(resolved).lexically_normal();
            auto type = readSerializedAssetType(resolvedPath);
            if (type.has_value() && type.value() == engine::Asset::AssetType::TEXTURE)
                addTexturePath(resolvedPath);
        }

        std::error_code scanError;
        for (std::filesystem::recursive_directory_iterator iterator(projectRoot, scanError);
             !scanError && iterator != std::filesystem::recursive_directory_iterator();
             iterator.increment(scanError))
        {
            std::error_code fileError;
            if (!iterator->is_regular_file(fileError) || fileError)
                continue;

            const std::filesystem::path path = iterator->path().lexically_normal();
            auto type = readSerializedAssetType(path);
            if (type.has_value() && type.value() == engine::Asset::AssetType::TEXTURE)
                addTexturePath(path);
        }

        std::sort(texturePaths.begin(), texturePaths.end(), [](const std::string &left, const std::string &right)
                  { return toLowerCopy(left) < toLowerCopy(right); });

        return texturePaths;
    }

    std::string resolveTexturePathForMaterialFile(const std::string &texturePath,
                                                  const std::filesystem::path &materialFilePath,
                                                  const std::filesystem::path &projectRoot)
    {
        if (texturePath.empty())
            return {};

        std::error_code errorCode;
        const std::filesystem::path materialDirectory = materialFilePath.parent_path();
        auto tryResolveByPortableFileName = [&](const std::string &rawPath) -> std::optional<std::string>
        {
            if (rawPath.empty())
                return std::nullopt;

            std::string portablePath = rawPath;
            std::replace(portablePath.begin(), portablePath.end(), '\\', '/');
            const size_t lastSlashPosition = portablePath.find_last_of('/');
            const std::string fileName = (lastSlashPosition == std::string::npos)
                                             ? portablePath
                                             : portablePath.substr(lastSlashPosition + 1u);
            if (fileName.empty())
                return std::nullopt;

            if (!materialDirectory.empty())
            {
                const std::filesystem::path directCandidate = makeAbsoluteNormalized(materialDirectory / fileName);
                errorCode.clear();
                if (std::filesystem::exists(directCandidate, errorCode) && !errorCode)
                    return directCandidate.string();

                const std::filesystem::path textureAssetCandidate =
                    makeAbsoluteNormalized(materialDirectory / (std::filesystem::path(fileName).stem().string() + ".tex.elixasset"));
                errorCode.clear();
                if (std::filesystem::exists(textureAssetCandidate, errorCode) && !errorCode)
                    return textureAssetCandidate.string();
            }

            return std::nullopt;
        };

        if (looksLikeWindowsAbsolutePath(texturePath))
        {
            if (auto remappedPath = tryResolveByPortableFileName(texturePath); remappedPath.has_value())
                return remappedPath.value();
            return texturePath;
        }

        const std::filesystem::path path(texturePath);
        if (path.is_absolute())
        {
            const std::filesystem::path normalizedAbsolutePath = makeAbsoluteNormalized(path);
            if (std::filesystem::exists(normalizedAbsolutePath, errorCode) && !errorCode)
                return normalizedAbsolutePath.string();

            if (auto remappedPath = tryResolveByPortableFileName(texturePath); remappedPath.has_value())
                return remappedPath.value();

            return normalizedAbsolutePath.string();
        }

        if (!materialDirectory.empty())
        {
            const std::filesystem::path materialRelativePath = makeAbsoluteNormalized(materialDirectory / path);
            if (std::filesystem::exists(materialRelativePath, errorCode) && !errorCode)
                return materialRelativePath.string();
        }

        const std::string projectRelativeResolved = resolveTexturePathAgainstProjectRoot(texturePath, projectRoot);
        if (!projectRelativeResolved.empty())
        {
            errorCode.clear();
            const std::filesystem::path projectRelativePath(projectRelativeResolved);
            if (std::filesystem::exists(projectRelativePath, errorCode) && !errorCode)
                return projectRelativeResolved;
        }

        return texturePath;
    }

    void normalizeMaterialTexturePaths(engine::CPUMaterial &material,
                                       const std::filesystem::path &materialFilePath,
                                       const std::filesystem::path &projectRoot)
    {
        const std::filesystem::path materialDirectory = materialFilePath.parent_path();
        material.albedoTexture = toMaterialTextureReferencePath(resolveTexturePathForMaterialFile(material.albedoTexture, materialFilePath, projectRoot), projectRoot, materialDirectory);
        material.normalTexture = toMaterialTextureReferencePath(resolveTexturePathForMaterialFile(material.normalTexture, materialFilePath, projectRoot), projectRoot, materialDirectory);
        material.ormTexture = toMaterialTextureReferencePath(resolveTexturePathForMaterialFile(material.ormTexture, materialFilePath, projectRoot), projectRoot, materialDirectory);
        material.emissiveTexture = toMaterialTextureReferencePath(resolveTexturePathForMaterialFile(material.emissiveTexture, materialFilePath, projectRoot), projectRoot, materialDirectory);
    }

    std::optional<std::filesystem::path> findCaseInsensitiveFileInDirectory(const std::filesystem::path &directory,
                                                                            const std::string &fileName)
    {
        if (directory.empty() || fileName.empty())
            return std::nullopt;

        std::error_code errorCode;
        if (!std::filesystem::exists(directory, errorCode) || errorCode)
            return std::nullopt;

        if (!std::filesystem::is_directory(directory, errorCode) || errorCode)
            return std::nullopt;

        const std::string loweredTarget = toLowerCopy(fileName);

        for (std::filesystem::directory_iterator iterator(directory, errorCode); !errorCode && iterator != std::filesystem::directory_iterator(); iterator.increment(errorCode))
        {
            const auto &entry = *iterator;
            std::error_code fileStatusError;
            if (!entry.is_regular_file(fileStatusError) || fileStatusError)
                continue;

            const std::string candidateName = entry.path().filename().string();
            if (toLowerCopy(candidateName) == loweredTarget)
                return entry.path().lexically_normal();
        }

        return std::nullopt;
    }

    std::string extractFileNamePortable(std::string path)
    {
        if (path.empty())
            return {};

        std::replace(path.begin(), path.end(), '\\', '/');

        const size_t lastSlashPosition = path.find_last_of('/');
        if (lastSlashPosition == std::string::npos)
            return path;

        if (lastSlashPosition + 1u >= path.size())
            return {};

        return path.substr(lastSlashPosition + 1u);
    }

    bool tryResolveCandidateTextureAssetPath(const std::filesystem::path &candidatePath,
                                             const std::filesystem::path &projectRoot,
                                             const std::filesystem::path &preferredImportDirectory,
                                             bool allowImportFromSource,
                                             std::string &outResolvedPath)
    {
        std::string resolvedSourcePath;
        if (!tryResolveCandidateTexturePath(candidatePath, resolvedSourcePath))
            return false;

        const std::string textureReferencePath = toMaterialTextureReferencePath(resolvedSourcePath,
                                                                                projectRoot,
                                                                                preferredImportDirectory,
                                                                                allowImportFromSource);
        if (textureReferencePath.empty())
            return false;

        const std::filesystem::path resolvedTexturePath = makeAbsoluteNormalized(std::filesystem::path(resolveTexturePathAgainstProjectRoot(textureReferencePath, projectRoot)));
        auto textureType = readSerializedAssetType(resolvedTexturePath);
        if (!textureType.has_value() || textureType.value() != engine::Asset::AssetType::TEXTURE)
            return false;

        outResolvedPath = resolvedTexturePath.string();
        return true;
    }

    std::string resolveTexturePathForMaterialExport(const std::string &rawTexturePath,
                                                    const std::filesystem::path &modelDirectory,
                                                    const std::filesystem::path &projectRoot,
                                                    const std::filesystem::path &textureSearchDirectory,
                                                    const std::unordered_map<std::string, std::string> &textureOverrides,
                                                    bool &resolved,
                                                    const std::filesystem::path &preferredImportDirectory = {},
                                                    bool allowImportFromSource = false)
    {
        resolved = true;
        if (rawTexturePath.empty())
            return {};

        auto overrideIt = textureOverrides.find(rawTexturePath);
        const std::string sourcePath = overrideIt != textureOverrides.end() && !overrideIt->second.empty()
                                           ? overrideIt->second
                                           : rawTexturePath;
        const std::filesystem::path texturePath(sourcePath);

        std::string resolvedPath;

        if (texturePath.is_absolute())
        {
            if (tryResolveCandidateTextureAssetPath(texturePath,
                                                    projectRoot,
                                                    preferredImportDirectory,
                                                    allowImportFromSource,
                                                    resolvedPath))
                return resolvedPath;

            resolved = false;
            return sourcePath;
        }

        if (!texturePath.empty() && tryResolveCandidateTextureAssetPath(makeAbsoluteNormalized(modelDirectory / texturePath),
                                                                        projectRoot,
                                                                        preferredImportDirectory,
                                                                        allowImportFromSource,
                                                                        resolvedPath))
            return resolvedPath;

        if (!projectRoot.empty() && !texturePath.empty() &&
            tryResolveCandidateTextureAssetPath(makeAbsoluteNormalized(projectRoot / texturePath),
                                                projectRoot,
                                                preferredImportDirectory,
                                                allowImportFromSource,
                                                resolvedPath))
            return resolvedPath;

        if (!textureSearchDirectory.empty())
        {
            const std::string textureFileName = extractFileNamePortable(sourcePath);
            std::array<std::string, 2> candidateNames = {textureFileName, {}};

            if (!textureFileName.empty())
                candidateNames[1] = std::filesystem::path(textureFileName).stem().string() + ".tex.elixasset";

            for (const auto &candidateName : candidateNames)
            {
                if (candidateName.empty())
                    continue;

                std::filesystem::path remappedPath = textureSearchDirectory / candidateName;
                if (auto caseInsensitiveMatch = findCaseInsensitiveFileInDirectory(textureSearchDirectory, candidateName);
                    caseInsensitiveMatch.has_value())
                    remappedPath = caseInsensitiveMatch.value();

                if (tryResolveCandidateTextureAssetPath(remappedPath,
                                                        projectRoot,
                                                        preferredImportDirectory,
                                                        allowImportFromSource,
                                                        resolvedPath))
                    return resolvedPath;
            }
        }

        resolved = false;
        return sourcePath;
    }
}

bool AssetDetailsView::canDraw(const Editor &editor) const
{
    return editor.m_detailsContext == Editor::DetailsContext::Asset &&
           !editor.m_selectedAssetPath.empty();
}

void AssetDetailsView::draw(Editor &editor)
{
    if (editor.m_selectedAssetPath.empty())
    {
        ImGui::TextUnformatted("Select an object or asset to view details");
        return;
    }

    if (!std::filesystem::exists(editor.m_selectedAssetPath))
    {
        ImGui::TextDisabled("Selected asset no longer exists");
        if (ImGui::Button("Clear Asset Selection"))
        {
            editor.m_selectedAssetPath.clear();
            editor.invalidateModelDetailsCache();
        }
        return;
    }

    auto project = editor.m_currentProject.lock();
    const std::filesystem::path projectRoot = project ? std::filesystem::path(project->fullPath) : std::filesystem::path{};
    const std::filesystem::path assetPath = editor.m_selectedAssetPath;
    const bool isDirectory = std::filesystem::is_directory(assetPath);
    const std::string extensionLower = toLowerCopy(assetPath.extension().string());

    ImGui::Text("Name: %s", assetPath.filename().string().c_str());
    ImGui::Text("Type: %s", isDirectory ? "Folder" : extensionLower.c_str());
    ImGui::TextWrapped("Path: %s", assetPath.string().c_str());

    if (!projectRoot.empty() && isPathWithinRoot(assetPath, projectRoot))
    {
        const std::string relativePath = toProjectRelativePathIfPossible(assetPath, projectRoot);
        ImGui::TextWrapped("Project path: %s", relativePath.c_str());
    }

    if (!isDirectory)
    {
        std::error_code fileSizeError;
        const auto fileSize = std::filesystem::file_size(assetPath, fileSizeError);
        if (!fileSizeError)
            ImGui::Text("File size: %.2f MB", static_cast<double>(fileSize) / (1024.0 * 1024.0));
    }

    if (isDirectory)
    {
        size_t entriesCount = 0;
        std::error_code iteratorError;
        for (std::filesystem::directory_iterator iterator(assetPath, iteratorError); !iteratorError && iterator != std::filesystem::directory_iterator(); iterator.increment(iteratorError))
            ++entriesCount;

        ImGui::Text("Entries: %zu", entriesCount);
        return;
    }

    if (extensionLower == ".elixmat")
    {
        if (ImGui::Button("Open Material Editor"))
            editor.openMaterialEditor(assetPath);

        auto materialAsset = engine::AssetsLoader::loadMaterial(assetPath.string());
        if (!materialAsset.has_value())
        {
            ImGui::Separator();
            ImGui::TextDisabled("Failed to parse material file");
            return;
        }

        const auto &material = materialAsset.value().material;
        ImGui::Separator();
        const std::string materialName = material.name.empty() ? assetPath.stem().string() : material.name;
        ImGui::Text("Material: %s", materialName.c_str());
        ImGui::TextWrapped("Albedo: %s", makeTextureAssetDisplayName(material.albedoTexture).c_str());
        ImGui::TextWrapped("Normal: %s", makeTextureAssetDisplayName(material.normalTexture).c_str());
        ImGui::TextWrapped("ORM: %s", makeTextureAssetDisplayName(material.ormTexture).c_str());
        ImGui::TextWrapped("Emissive: %s", makeTextureAssetDisplayName(material.emissiveTexture).c_str());
        return;
    }

    if (isTextureAssetPath(assetPath))
    {
        ImGui::Separator();
        ImGui::Text("Texture format: %s", extensionLower.c_str());
        ImGui::TextWrapped("Use drag-and-drop into material slots to assign this texture.");
        return;
    }

    if (isScriptAssetPath(assetPath) || isEditableTextPath(assetPath))
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Script / text asset");
        if (ImGui::Button("Open In Text Editor"))
            editor.openTextDocument(assetPath);
        return;
    }

    if (!isModelAssetPath(assetPath))
        return;

    const engine::ModelAsset *modelAsset = editor.ensureModelAssetLoaded(assetPath.string());
    ImGui::Separator();
    ImGui::TextUnformatted("Model Import");

    if (!modelAsset)
    {
        ImGui::TextDisabled("Failed to load model metadata");
        if (ImGui::Button("Retry"))
        {
            editor.ensureModelAssetLoaded(assetPath.string());
            editor.invalidateModelDetailsCache();
        }
        return;
    }

    const std::filesystem::path modelDirectory = assetPath.parent_path();

    if (editor.m_lastModelDetailsAssetPath != assetPath)
    {
        editor.m_lastModelDetailsAssetPath = assetPath;
        editor.invalidateModelDetailsCache();

        const std::filesystem::path defaultExportPath = assetPath.parent_path() / (assetPath.stem().string() + "_Materials");
        std::memset(editor.m_modelMaterialsExportDirectory, 0, sizeof(editor.m_modelMaterialsExportDirectory));
        std::strncpy(editor.m_modelMaterialsExportDirectory, defaultExportPath.string().c_str(), sizeof(editor.m_modelMaterialsExportDirectory) - 1);

        std::memset(editor.m_modelMaterialsTextureSearchDirectory, 0, sizeof(editor.m_modelMaterialsTextureSearchDirectory));
        std::strncpy(editor.m_modelMaterialsTextureSearchDirectory, assetPath.parent_path().string().c_str(), sizeof(editor.m_modelMaterialsTextureSearchDirectory) - 1);

        editor.m_modelTextureManualOverrides.clear();
        editor.m_selectedUnresolvedTexturePath.clear();
        std::memset(editor.m_selectedTextureOverrideBuffer, 0, sizeof(editor.m_selectedTextureOverrideBuffer));
    }

    std::filesystem::path textureSearchDirectory = std::filesystem::path(editor.m_modelMaterialsTextureSearchDirectory);
    if (!textureSearchDirectory.empty())
    {
        if (textureSearchDirectory.is_relative() && !projectRoot.empty())
            textureSearchDirectory = projectRoot / textureSearchDirectory;

        textureSearchDirectory = makeAbsoluteNormalized(textureSearchDirectory);
    }

    if (editor.m_modelDetailsCacheDirty ||
        editor.m_modelDetailsCacheAssetPath != assetPath ||
        editor.m_modelDetailsCacheSearchDirectory != textureSearchDirectory)
    {
        editor.rebuildModelDetailsCache(*modelAsset, modelDirectory, projectRoot, textureSearchDirectory);
        editor.m_modelDetailsCacheAssetPath = assetPath;
    }

    auto &materials = editor.m_modelDetailsCache.materials;
    auto &unresolvedTexturePathList = editor.m_modelDetailsCache.unresolvedTexturePaths;

    if (!editor.m_selectedUnresolvedTexturePath.empty() &&
        std::find(unresolvedTexturePathList.begin(), unresolvedTexturePathList.end(), editor.m_selectedUnresolvedTexturePath) == unresolvedTexturePathList.end())
    {
        editor.m_selectedUnresolvedTexturePath.clear();
        std::memset(editor.m_selectedTextureOverrideBuffer, 0, sizeof(editor.m_selectedTextureOverrideBuffer));
    }

    ImGui::Text("Meshes: %zu", modelAsset->meshes.size());
    ImGui::Text("Vertices: %zu", editor.m_modelDetailsCache.totalVertexCount);
    ImGui::Text("Indices: %zu", editor.m_modelDetailsCache.totalIndexCount);
    ImGui::Text("Unique materials: %zu", materials.size());
    ImGui::Text("Unresolved texture paths: %zu", unresolvedTexturePathList.size());
    ImGui::Text("Has skeleton: %s", editor.m_modelDetailsCache.hasSkeleton ? "Yes" : "No");
    ImGui::Text("Animations: %zu", editor.m_modelDetailsCache.animationCount);

    if (editor.m_modelDetailsCache.animationCount > 0)
    {
        if (ImGui::Button("Export Animations"))
        {
            const std::string stem = assetPath.stem().string();
            const std::string strippedStem = stem.rfind(".model") != std::string::npos
                                                 ? stem.substr(0, stem.rfind(".model"))
                                                 : stem;
            const std::filesystem::path outputPath = assetPath.parent_path() / (strippedStem + ".anim.elixasset");
            if (engine::AssetsLoader::exportAnimationsFromModel(assetPath.string(), outputPath.string()))
                editor.m_notificationManager.showSuccess("Animations exported to " + outputPath.filename().string());
            else
                editor.m_notificationManager.showError("Failed to export animations");
        }
    }

    if (ImGui::CollapsingHeader("Texture Resolver", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextWrapped("Search folder remaps unresolved texture paths by filename (directory is replaced, filename stays unless you set manual override).");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("Search folder", editor.m_modelMaterialsTextureSearchDirectory, sizeof(editor.m_modelMaterialsTextureSearchDirectory)))
        {
            textureSearchDirectory = std::filesystem::path(editor.m_modelMaterialsTextureSearchDirectory);
            if (!textureSearchDirectory.empty())
            {
                if (textureSearchDirectory.is_relative() && !projectRoot.empty())
                    textureSearchDirectory = projectRoot / textureSearchDirectory;

                textureSearchDirectory = makeAbsoluteNormalized(textureSearchDirectory);
            }

            editor.m_modelDetailsCacheDirty = true;
        }

        if (ImGui::Button("Use Model Directory"))
        {
            std::memset(editor.m_modelMaterialsTextureSearchDirectory, 0, sizeof(editor.m_modelMaterialsTextureSearchDirectory));
            std::strncpy(editor.m_modelMaterialsTextureSearchDirectory, assetPath.parent_path().string().c_str(), sizeof(editor.m_modelMaterialsTextureSearchDirectory) - 1);
            textureSearchDirectory = makeAbsoluteNormalized(std::filesystem::path(editor.m_modelMaterialsTextureSearchDirectory));
            editor.m_modelDetailsCacheDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply Folder To All Unresolved"))
        {
            if (textureSearchDirectory.empty() || !std::filesystem::exists(textureSearchDirectory) || !std::filesystem::is_directory(textureSearchDirectory))
            {
                editor.m_notificationManager.showError("Search folder is invalid");
            }
            else if (unresolvedTexturePathList.empty())
            {
                editor.m_notificationManager.showInfo("No unresolved texture paths");
            }
            else
            {
                size_t mappedCount = 0;
                size_t missingCount = 0;
                bool overridesChanged = false;
                auto previewOverrides = editor.m_modelTextureManualOverrides;

                for (const auto &unresolvedPath : unresolvedTexturePathList)
                {
                    const std::string fileName = extractFileNamePortable(unresolvedPath);

                    if (fileName.empty())
                    {
                        ++missingCount;
                        continue;
                    }

                    std::filesystem::path remappedPath;
                    std::array<std::string, 2> candidateNames = {fileName, std::filesystem::path(fileName).stem().string() + ".tex.elixasset"};

                    for (const auto &candidateName : candidateNames)
                    {
                        if (candidateName.empty())
                            continue;

                        std::filesystem::path candidatePath = textureSearchDirectory / candidateName;
                        if (auto caseInsensitiveMatch = findCaseInsensitiveFileInDirectory(textureSearchDirectory, candidateName);
                            caseInsensitiveMatch.has_value())
                            candidatePath = caseInsensitiveMatch.value();

                        std::error_code existsError;
                        if (std::filesystem::exists(candidatePath, existsError) && !existsError)
                        {
                            remappedPath = candidatePath;
                            break;
                        }
                    }

                    if (remappedPath.empty())
                    {
                        const std::string defaultName = !candidateNames[1].empty() ? candidateNames[1] : candidateNames[0];
                        remappedPath = textureSearchDirectory / defaultName;
                    }

                    remappedPath = remappedPath.lexically_normal();
                    std::string remappedPathString = remappedPath.string();
                    previewOverrides[unresolvedPath] = remappedPathString;

                    bool remappedResolved = true;
                    const std::string resolvedPreviewPath = resolveTexturePathForMaterialExport(unresolvedPath,
                                                                                                modelDirectory,
                                                                                                projectRoot,
                                                                                                textureSearchDirectory,
                                                                                                previewOverrides,
                                                                                                remappedResolved,
                                                                                                textureSearchDirectory,
                                                                                                false);
                    if (remappedResolved && !resolvedPreviewPath.empty())
                    {
                        remappedPathString = resolvedPreviewPath;
                        previewOverrides[unresolvedPath] = remappedPathString;
                    }

                    auto overrideIt = editor.m_modelTextureManualOverrides.find(unresolvedPath);
                    if (overrideIt == editor.m_modelTextureManualOverrides.end() || overrideIt->second != remappedPathString)
                    {
                        editor.m_modelTextureManualOverrides[unresolvedPath] = remappedPathString;
                        overridesChanged = true;
                    }

                    if (remappedResolved)
                        ++mappedCount;
                    else
                        ++missingCount;
                }

                if (overridesChanged)
                    editor.m_modelDetailsCacheDirty = true;

                if (mappedCount > 0)
                    editor.m_notificationManager.showSuccess("Mapped " + std::to_string(mappedCount) + " texture path(s)");

                if (missingCount > 0)
                    editor.m_notificationManager.showWarning(std::to_string(missingCount) + " path(s) still missing after remap");
            }
        }

        if (unresolvedTexturePathList.empty())
            ImGui::TextUnformatted("All discovered texture paths are resolved with current rules.");
        else
        {
            ImGui::TextUnformatted("Unresolved texture paths");
            if (ImGui::BeginListBox("##UnresolvedTextureList", ImVec2(0.0f, 140.0f)))
            {
                for (const auto &texturePath : unresolvedTexturePathList)
                {
                    const bool selected = texturePath == editor.m_selectedUnresolvedTexturePath;
                    if (ImGui::Selectable(texturePath.c_str(), selected))
                    {
                        editor.m_selectedUnresolvedTexturePath = texturePath;
                        std::memset(editor.m_selectedTextureOverrideBuffer, 0, sizeof(editor.m_selectedTextureOverrideBuffer));

                        auto overrideIt = editor.m_modelTextureManualOverrides.find(texturePath);
                        const std::string &overrideValue = overrideIt != editor.m_modelTextureManualOverrides.end() ? overrideIt->second : "";
                        std::strncpy(editor.m_selectedTextureOverrideBuffer, overrideValue.c_str(), sizeof(editor.m_selectedTextureOverrideBuffer) - 1);
                    }
                }

                ImGui::EndListBox();
            }

            if (!editor.m_selectedUnresolvedTexturePath.empty())
            {
                ImGui::TextWrapped("Selected: %s", editor.m_selectedUnresolvedTexturePath.c_str());
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("Override path", editor.m_selectedTextureOverrideBuffer, sizeof(editor.m_selectedTextureOverrideBuffer));

                if (ImGui::Button("Apply Override"))
                {
                    const std::string overridePath = editor.m_selectedTextureOverrideBuffer;
                    auto overrideIt = editor.m_modelTextureManualOverrides.find(editor.m_selectedUnresolvedTexturePath);
                    if (overrideIt == editor.m_modelTextureManualOverrides.end() || overrideIt->second != overridePath)
                    {
                        editor.m_modelTextureManualOverrides[editor.m_selectedUnresolvedTexturePath] = overridePath;
                        editor.m_modelDetailsCacheDirty = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Override"))
                {
                    const size_t erasedCount = editor.m_modelTextureManualOverrides.erase(editor.m_selectedUnresolvedTexturePath);
                    std::memset(editor.m_selectedTextureOverrideBuffer, 0, sizeof(editor.m_selectedTextureOverrideBuffer));
                    if (erasedCount > 0)
                        editor.m_modelDetailsCacheDirty = true;
                }

                bool previewResolved = true;
                const std::string previewPath = resolveTexturePathForMaterialExport(editor.m_selectedUnresolvedTexturePath,
                                                                                    modelDirectory,
                                                                                    projectRoot,
                                                                                    textureSearchDirectory,
                                                                                    editor.m_modelTextureManualOverrides,
                                                                                    previewResolved,
                                                                                    textureSearchDirectory,
                                                                                    false);
                ImGui::TextWrapped("Preview: %s", previewPath.empty() ? "<None>" : previewPath.c_str());
                ImGui::TextDisabled("%s", previewResolved ? "Status: Resolved" : "Status: Unresolved");
            }
        }

        if (!editor.m_modelTextureManualOverrides.empty() && ImGui::TreeNode("Manual overrides"))
        {
            for (auto overrideIterator = editor.m_modelTextureManualOverrides.begin(); overrideIterator != editor.m_modelTextureManualOverrides.end();)
            {
                const std::string originalPath = overrideIterator->first;
                const std::string overridePath = overrideIterator->second;

                ImGui::PushID(originalPath.c_str());
                ImGui::TextWrapped("%s", originalPath.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    if (editor.m_selectedUnresolvedTexturePath == originalPath)
                    {
                        editor.m_selectedUnresolvedTexturePath.clear();
                        std::memset(editor.m_selectedTextureOverrideBuffer, 0, sizeof(editor.m_selectedTextureOverrideBuffer));
                    }

                    overrideIterator = editor.m_modelTextureManualOverrides.erase(overrideIterator);
                    editor.m_modelDetailsCacheDirty = true;
                    ImGui::PopID();
                    continue;
                }
                ImGui::TextWrapped(" -> %s", overridePath.c_str());
                ImGui::PopID();

                ++overrideIterator;
            }

            ImGui::TreePop();
        }
    }

    if (ImGui::CollapsingHeader("Imported materials", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::BeginTable("ModelMaterialTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Material");
            ImGui::TableSetupColumn("Meshes");
            ImGui::TableSetupColumn("Albedo");
            ImGui::TableSetupColumn("Normal");
            ImGui::TableSetupColumn("ORM");
            ImGui::TableSetupColumn("Emissive");
            ImGui::TableHeadersRow();

            const size_t maxRowsToShow = std::min<size_t>(materials.size(), 128);
            for (size_t materialIndex = 0; materialIndex < maxRowsToShow; ++materialIndex)
            {
                const auto &entry = materials[materialIndex];

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(entry.material.name.empty() ? "<Unnamed>" : entry.material.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%zu", entry.meshUsageCount);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(entry.albedoDisplayPath.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(entry.normalDisplayPath.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(entry.ormDisplayPath.c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(entry.emissiveDisplayPath.c_str());
            }

            ImGui::EndTable();

            if (materials.size() > maxRowsToShow)
                ImGui::TextDisabled("Showing first %zu of %zu materials", maxRowsToShow, materials.size());
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Material Export");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Output folder", editor.m_modelMaterialsExportDirectory, sizeof(editor.m_modelMaterialsExportDirectory));

    if (ImGui::Button("Use Model Folder"))
    {
        std::memset(editor.m_modelMaterialsExportDirectory, 0, sizeof(editor.m_modelMaterialsExportDirectory));
        std::strncpy(editor.m_modelMaterialsExportDirectory, assetPath.parent_path().string().c_str(), sizeof(editor.m_modelMaterialsExportDirectory) - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Use Default Export Folder"))
    {
        const std::filesystem::path defaultExportPath = assetPath.parent_path() / (assetPath.stem().string() + "_Materials");
        std::memset(editor.m_modelMaterialsExportDirectory, 0, sizeof(editor.m_modelMaterialsExportDirectory));
        std::strncpy(editor.m_modelMaterialsExportDirectory, defaultExportPath.string().c_str(), sizeof(editor.m_modelMaterialsExportDirectory) - 1);
    }

    const auto *selectedStaticMeshComponent = editor.m_selectedEntity ? editor.m_selectedEntity->getComponent<engine::StaticMeshComponent>() : nullptr;
    const auto *selectedSkeletalMeshComponent = editor.m_selectedEntity ? editor.m_selectedEntity->getComponent<engine::SkeletalMeshComponent>() : nullptr;
    const bool selectedEntityCanReceiveMaterials = selectedStaticMeshComponent || selectedSkeletalMeshComponent;
    const size_t selectedEntitySlotCount = selectedStaticMeshComponent     ? selectedStaticMeshComponent->getMaterialSlotCount()
                                           : selectedSkeletalMeshComponent ? selectedSkeletalMeshComponent->getMaterialSlotCount()
                                                                           : 0;
    const bool selectedEntitySlotCountMatchesModel = selectedEntitySlotCount == modelAsset->meshes.size();

    auto runMaterialExport = [&](bool applyToSelectedEntity)
    {
        const std::filesystem::path exportPath = std::filesystem::path(editor.m_modelMaterialsExportDirectory);
        if (exportPath.empty())
        {
            editor.m_notificationManager.showError("Output folder is empty");
            return;
        }

        std::vector<std::string> perMeshMaterialPaths;
        std::vector<std::string> *outputBindings = applyToSelectedEntity ? &perMeshMaterialPaths : nullptr;

        if (!editor.exportModelMaterials(assetPath,
                                         exportPath,
                                         textureSearchDirectory,
                                         editor.m_modelTextureManualOverrides,
                                         outputBindings))
        {
            editor.m_notificationManager.showError("Material export failed");
            return;
        }

        if (!applyToSelectedEntity)
            return;

        if (!editor.applyPerMeshMaterialPathsToSelectedEntity(perMeshMaterialPaths))
        {
            editor.m_notificationManager.showWarning("Materials exported, but auto-apply to selected entity failed");
            return;
        }

        editor.m_notificationManager.showSuccess("Exported and applied materials to selected entity");
    };

    auto runApplyMaterialsOnly = [&]()
    {
        auto project = editor.m_currentProject.lock();
        const std::filesystem::path projectRoot = project ? std::filesystem::path(project->fullPath) : std::filesystem::path{};

        std::filesystem::path materialsDirectory = std::filesystem::path(editor.m_modelMaterialsExportDirectory);
        if (materialsDirectory.empty())
        {
            editor.m_notificationManager.showError("Output folder is empty");
            return;
        }

        if (materialsDirectory.is_relative() && !projectRoot.empty())
            materialsDirectory = projectRoot / materialsDirectory;

        materialsDirectory = makeAbsoluteNormalized(materialsDirectory);

        std::vector<std::string> perMeshMaterialPaths;
        size_t matchedSlots = 0;
        if (!editor.buildPerMeshMaterialPathsFromDirectory(*modelAsset, materialsDirectory, perMeshMaterialPaths, matchedSlots))
        {
            editor.m_notificationManager.showError("Failed to resolve materials from output folder");
            return;
        }

        if (!editor.applyPerMeshMaterialPathsToSelectedEntity(perMeshMaterialPaths))
        {
            editor.m_notificationManager.showWarning("Resolved materials, but failed to apply to selected entity");
            return;
        }

        editor.m_notificationManager.showSuccess("Applied " + std::to_string(matchedSlots) + " material slot(s) from output folder");
    };

    if (ImGui::Button("Export Materials"))
    {
        runMaterialExport(false);
    }

    ImGui::SameLine();
    if (!selectedEntityCanReceiveMaterials)
        ImGui::BeginDisabled();

    if (ImGui::Button("Export + Apply To Selected"))
        runMaterialExport(true);

    if (!selectedEntityCanReceiveMaterials)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (!selectedEntityCanReceiveMaterials)
        ImGui::BeginDisabled();

    if (ImGui::Button("Apply Materials To Selected"))
        runApplyMaterialsOnly();

    if (!selectedEntityCanReceiveMaterials)
        ImGui::EndDisabled();

    if (!editor.m_selectedEntity)
        ImGui::TextDisabled("Select a scene entity with mesh component to enable auto-apply.");
    else if (!selectedEntityCanReceiveMaterials)
        ImGui::TextDisabled("Selected entity has no mesh component.");
    else if (!selectedEntitySlotCountMatchesModel)
        ImGui::TextDisabled("Slot mismatch: selected entity has %zu slot(s), model has %zu mesh slot(s).", selectedEntitySlotCount, modelAsset->meshes.size());
    else
        ImGui::TextDisabled("Auto-apply target: '%s' (%zu slot(s)).", editor.m_selectedEntity->getName().c_str(), selectedEntitySlotCount);

    ImGui::TextWrapped("Set a texture search folder, optionally add manual overrides for unresolved paths, then export or apply materials.");
}

ELIX_NESTED_NAMESPACE_END
