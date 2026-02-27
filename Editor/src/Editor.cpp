#include "Editor/Editor.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Scripting/ScriptsRegister.hpp"
#include "Engine/Components/ScriptComponent.hpp"
#include "Engine/Components/CameraComponent.hpp"
#include "Engine/Components/RigidBodyComponent.hpp"
#include "Engine/Primitives.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Assets/AssetsSerializer.hpp"
#include "Engine/Render/ObjectIdEncoding.hpp"
#include "Engine/Shaders/ShaderCompiler.hpp"

#include "Engine/Primitives.hpp"
#include "Engine/Components/CollisionComponent.hpp"

#include "Engine/PluginSystem/PluginLoader.hpp"
#include "Engine/Runtime/EngineConfig.hpp"
#include "Engine/Utilities/ImageUtilities.hpp"

#include "Editor/FileHelper.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <iostream>
#include <cstring>

#include <GLFW/glfw3.h>

#include <fstream>
#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <limits>
#include <cmath>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstdlib>

#include "ImGuizmo.h"
#include <nlohmann/json.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

namespace
{
    std::string quoteShellArgument(const std::filesystem::path &path)
    {
        std::string value = path.string();
        std::string escaped;
        escaped.reserve(value.size() + 2);

        for (const char character : value)
        {
#if defined(_WIN32)
            if (character == '"')
#else
            if (character == '"' || character == '\\')
#endif
                escaped.push_back('\\');

            escaped.push_back(character);
        }

        return "\"" + escaped + "\"";
    }

    std::string joinDetectedIdeNames(const std::vector<elix::engine::EngineConfig::IdeInfo> &ides)
    {
        if (ides.empty())
            return "none";

        std::string joinedNames;
        for (size_t i = 0; i < ides.size(); ++i)
        {
            if (i > 0)
                joinedNames += ", ";

            joinedNames += ides[i].displayName;
        }

        return joinedNames;
    }

    bool syncProjectCompileCommands(const elix::editor::Project &project)
    {
        const std::filesystem::path projectRoot = project.fullPath;
        const std::filesystem::path buildDirectory = project.buildDir.empty()
                                                         ? projectRoot / "build"
                                                         : std::filesystem::path(project.buildDir);

        const std::filesystem::path sourcePath = buildDirectory / "compile_commands.json";
        const std::filesystem::path destinationPath = projectRoot / "compile_commands.json";

        if (!std::filesystem::exists(sourcePath))
            return false;

        std::error_code errorCode;
        std::filesystem::copy_file(sourcePath, destinationPath, std::filesystem::copy_options::overwrite_existing, errorCode);
        return !errorCode;
    }

    std::filesystem::path resolveCMakeExecutablePath()
    {
#if defined(_WIN32)
        constexpr const char *cmakeExecutableName = "cmake.exe";
#else
        constexpr const char *cmakeExecutableName = "cmake";
#endif

        auto isExistingFile = [](const std::filesystem::path &path) -> bool
        {
            if (path.empty())
                return false;

            std::error_code errorCode;
            return std::filesystem::exists(path, errorCode) && std::filesystem::is_regular_file(path, errorCode);
        };

        if (const char *envPath = std::getenv("VELIX_CMAKE"); envPath && *envPath)
        {
            std::filesystem::path candidate(envPath);
            if (isExistingFile(candidate))
                return candidate;
        }

        std::filesystem::path engineRoot = FileHelper::getExecutablePath();
        if (engineRoot.filename() == "bin")
            engineRoot = engineRoot.parent_path();

        const std::vector<std::filesystem::path> bundledCandidates = {
            engineRoot / "tools" / "cmake" / "bin" / cmakeExecutableName,
            engineRoot / "cmake" / "bin" / cmakeExecutableName,
            engineRoot / "third_party" / "cmake" / "bin" / cmakeExecutableName,
            engineRoot / "bin" / cmakeExecutableName,
            engineRoot / cmakeExecutableName};

        for (const auto &candidate : bundledCandidates)
        {
            if (isExistingFile(candidate))
                return candidate;
        }

        return std::filesystem::path(cmakeExecutableName);
    }

    std::string makeExecutableCommandToken(const std::filesystem::path &executablePath)
    {
        const std::string value = executablePath.string();
        const bool hasDirectorySeparators = value.find('/') != std::string::npos || value.find('\\') != std::string::npos;
        const bool hasWhitespace = value.find_first_of(" \t") != std::string::npos;

        if (executablePath.is_absolute() || hasDirectorySeparators || hasWhitespace)
            return quoteShellArgument(executablePath);

        return value;
    }

    std::filesystem::path findGameModuleLibraryPath(const std::filesystem::path &buildDirectory)
    {
        if (buildDirectory.empty() || !std::filesystem::exists(buildDirectory))
            return {};

        const std::vector<std::filesystem::path> searchRoots = {
            buildDirectory,
            buildDirectory / "Release",
            buildDirectory / "RelWithDebInfo",
            buildDirectory / "Debug",
            buildDirectory / "MinSizeRel"};

        const std::vector<std::string> moduleNames = {
            std::string("GameModule") + SHARED_LIB_EXTENSION,
            std::string("libGameModule") + SHARED_LIB_EXTENSION};

        for (const auto &searchRoot : searchRoots)
        {
            if (!std::filesystem::exists(searchRoot))
                continue;

            for (const auto &moduleName : moduleNames)
            {
                const auto candidatePath = searchRoot / moduleName;
                if (std::filesystem::exists(candidatePath) && std::filesystem::is_regular_file(candidatePath))
                    return candidatePath;
            }
        }

        for (const auto &entry : std::filesystem::recursive_directory_iterator(buildDirectory))
        {
            if (!entry.is_regular_file())
                continue;

            const auto path = entry.path();
            if (path.extension() != SHARED_LIB_EXTENSION)
                continue;

            const std::string stem = path.stem().string();
            if (stem == "GameModule" || stem == "libGameModule")
                return path;

            if (path.filename().string().find("GameModule") != std::string::npos)
                return path;
        }

        return {};
    }

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

    VkFormat getLdrTextureFormat(TextureUsage usage)
    {
        return usage == TextureUsage::Data ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
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

    std::string toMaterialTextureReferencePath(const std::string &texturePath, const std::filesystem::path &projectRoot)
    {
        if (texturePath.empty())
            return {};

        std::filesystem::path resolvedPath = resolveTexturePathAgainstProjectRoot(texturePath, projectRoot);
        resolvedPath = resolvedPath.lexically_normal();

        std::string extensionLower = toLowerCopy(resolvedPath.extension().string());
        if (extensionLower != ".elixasset")
        {
            std::filesystem::path candidateAssetPath = resolvedPath;
            candidateAssetPath.replace_extension(".tex.elixasset");

            std::error_code existsError;
            if (std::filesystem::exists(candidateAssetPath, existsError) && !existsError)
                resolvedPath = candidateAssetPath.lexically_normal();
            else
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

        if (looksLikeWindowsAbsolutePath(texturePath))
            return texturePath;

        const std::filesystem::path path(texturePath);
        if (path.is_absolute())
            return makeAbsoluteNormalized(path).string();

        std::error_code errorCode;
        const std::filesystem::path materialDirectory = materialFilePath.parent_path();
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
        material.albedoTexture = toMaterialTextureReferencePath(resolveTexturePathForMaterialFile(material.albedoTexture, materialFilePath, projectRoot), projectRoot);
        material.normalTexture = toMaterialTextureReferencePath(resolveTexturePathForMaterialFile(material.normalTexture, materialFilePath, projectRoot), projectRoot);
        material.ormTexture = toMaterialTextureReferencePath(resolveTexturePathForMaterialFile(material.ormTexture, materialFilePath, projectRoot), projectRoot);
        material.emissiveTexture = toMaterialTextureReferencePath(resolveTexturePathForMaterialFile(material.emissiveTexture, materialFilePath, projectRoot), projectRoot);
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

    std::string resolveTexturePathForMaterialExport(const std::string &rawTexturePath,
                                                    const std::filesystem::path &modelDirectory,
                                                    const std::filesystem::path &projectRoot,
                                                    const std::filesystem::path &textureSearchDirectory,
                                                    const std::unordered_map<std::string, std::string> &textureOverrides,
                                                    bool &resolved)
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
            if (tryResolveCandidateTexturePath(texturePath, resolvedPath))
                return resolvedPath;

            resolved = false;
            return sourcePath;
        }

        if (!texturePath.empty() && tryResolveCandidateTexturePath(makeAbsoluteNormalized(modelDirectory / texturePath), resolvedPath))
            return resolvedPath;

        if (!projectRoot.empty() && !texturePath.empty() &&
            tryResolveCandidateTexturePath(makeAbsoluteNormalized(projectRoot / texturePath), resolvedPath))
            return resolvedPath;

        if (!textureSearchDirectory.empty())
        {
            const std::string textureFileName = extractFileNamePortable(sourcePath);
            if (!textureFileName.empty() &&
                tryResolveCandidateTexturePath(makeAbsoluteNormalized(textureSearchDirectory / textureFileName), resolvedPath))
                return resolvedPath;
        }

        resolved = false;
        return sourcePath;
    }

    const TextEditor::LanguageDefinition &jsonLanguageDefinition()
    {
        static bool initialized = false;
        static TextEditor::LanguageDefinition languageDefinition;

        if (!initialized)
        {
            initialized = true;
            languageDefinition.mName = "JSON";
            languageDefinition.mCaseSensitive = true;
            languageDefinition.mCommentStart = "/*";
            languageDefinition.mCommentEnd = "*/";
            languageDefinition.mSingleLineComment = "//";
            languageDefinition.mAutoIndentation = true;
            languageDefinition.mKeywords = {"true", "false", "null"};

            languageDefinition.mTokenRegexStrings = {
                {R"("([^"\\]|\\.)*")", TextEditor::PaletteIndex::String},
                {R"([-+]?[0-9]+(\.[0-9]+)?([eE][-+]?[0-9]+)?)", TextEditor::PaletteIndex::Number},
                {R"([{}\[\],:])", TextEditor::PaletteIndex::Punctuation}};
        }

        return languageDefinition;
    }

    const TextEditor::LanguageDefinition &iniLanguageDefinition()
    {
        static bool initialized = false;
        static TextEditor::LanguageDefinition languageDefinition;

        if (!initialized)
        {
            initialized = true;
            languageDefinition.mName = "INI/Config";
            languageDefinition.mCaseSensitive = false;
            languageDefinition.mCommentStart = "";
            languageDefinition.mCommentEnd = "";
            languageDefinition.mSingleLineComment = ";";
            languageDefinition.mAutoIndentation = false;

            languageDefinition.mTokenRegexStrings = {
                {R"(\[[^\]]+\])", TextEditor::PaletteIndex::PreprocIdentifier},
                {R"([A-Za-z_][A-Za-z0-9_\.\-]*(?=\s*=))", TextEditor::PaletteIndex::Identifier},
                {R"("([^"\\]|\\.)*")", TextEditor::PaletteIndex::String},
                {R"([-+]?[0-9]+(\.[0-9]+)?)", TextEditor::PaletteIndex::Number},
                {R"(=)", TextEditor::PaletteIndex::Punctuation}};
        }

        return languageDefinition;
    }

    bool computeLocalBoundsFromMeshes(const std::vector<elix::engine::CPUMesh> &meshes, glm::vec3 &outMin, glm::vec3 &outMax)
    {
        outMin = glm::vec3(std::numeric_limits<float>::max());
        outMax = glm::vec3(std::numeric_limits<float>::lowest());
        bool hasVertexData = false;

        for (const auto &mesh : meshes)
        {
            if (mesh.vertexStride < sizeof(glm::vec3) || mesh.vertexData.empty())
                continue;

            const size_t vertexCount = mesh.vertexData.size() / mesh.vertexStride;
            const uint8_t *basePtr = mesh.vertexData.data();

            for (size_t i = 0; i < vertexCount; ++i)
            {
                glm::vec3 position{0.0f};
                std::memcpy(&position, basePtr + i * mesh.vertexStride, sizeof(glm::vec3));

                outMin = glm::min(outMin, position);
                outMax = glm::max(outMax, position);
                hasVertexData = true;
            }
        }

        return hasVertexData;
    }

    struct ColliderHandleProjection
    {
        int handleId = 0;
        ImVec2 screenPos{0.0f, 0.0f};
        glm::vec2 axisScreenDir{1.0f, 0.0f};
        float worldPerPixel{0.0f};
        float screenDistance{0.0f};
        bool valid{false};
    };

    glm::mat4 composeTransform(const glm::vec3 &position, const glm::quat &rotation)
    {
        return glm::translate(glm::mat4(1.0f), position) * glm::toMat4(rotation);
    }

    glm::mat4 shapeLocalPoseToMatrix(const physx::PxShape *shape)
    {
        if (!shape)
            return glm::mat4(1.0f);

        const physx::PxTransform pose = shape->getLocalPose();
        const glm::vec3 position(pose.p.x, pose.p.y, pose.p.z);
        const glm::quat rotation(pose.q.w, pose.q.x, pose.q.y, pose.q.z);
        return composeTransform(position, rotation);
    }

    glm::vec3 transformPoint(const glm::mat4 &matrix, const glm::vec3 &point)
    {
        return glm::vec3(matrix * glm::vec4(point, 1.0f));
    }

    glm::vec3 transformDirection(const glm::mat4 &matrix, const glm::vec3 &direction)
    {
        const glm::vec3 transformed = glm::vec3(matrix * glm::vec4(direction, 0.0f));
        const float lengthSquared = glm::dot(transformed, transformed);
        if (lengthSquared <= 0.000001f)
            return direction;
        return transformed / std::sqrt(lengthSquared);
    }

    bool worldToScreen(const glm::vec3 &worldPosition,
                       const glm::mat4 &view,
                       const glm::mat4 &projection,
                       const ImVec2 &viewportMin,
                       const ImVec2 &viewportSize,
                       ImVec2 &outScreen)
    {
        const glm::vec4 clipPosition = projection * view * glm::vec4(worldPosition, 1.0f);
        if (clipPosition.w <= 0.00001f)
            return false;

        const glm::vec3 ndc = glm::vec3(clipPosition) / clipPosition.w;
        outScreen.x = viewportMin.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
        outScreen.y = viewportMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y;

        return ndc.z >= -1.0f && ndc.z <= 1.0f;
    }

    void drawLine3D(ImDrawList *drawList,
                    const glm::vec3 &from,
                    const glm::vec3 &to,
                    const glm::mat4 &view,
                    const glm::mat4 &projection,
                    const ImVec2 &viewportMin,
                    const ImVec2 &viewportSize,
                    ImU32 color,
                    float thickness)
    {
        if (!drawList)
            return;

        ImVec2 fromScreen;
        ImVec2 toScreen;
        const bool fromVisible = worldToScreen(from, view, projection, viewportMin, viewportSize, fromScreen);
        const bool toVisible = worldToScreen(to, view, projection, viewportMin, viewportSize, toScreen);
        if (!fromVisible || !toVisible)
            return;

        drawList->AddLine(fromScreen, toScreen, color, thickness);
    }

    ColliderHandleProjection makeHandleProjection(int handleId,
                                                  const glm::vec3 &originWorld,
                                                  const glm::vec3 &handleWorld,
                                                  const glm::mat4 &view,
                                                  const glm::mat4 &projection,
                                                  const ImVec2 &viewportMin,
                                                  const ImVec2 &viewportSize)
    {
        ColliderHandleProjection result;
        result.handleId = handleId;

        ImVec2 originScreen;
        ImVec2 handleScreen;
        if (!worldToScreen(originWorld, view, projection, viewportMin, viewportSize, originScreen) ||
            !worldToScreen(handleWorld, view, projection, viewportMin, viewportSize, handleScreen))
            return result;

        const glm::vec2 delta(handleScreen.x - originScreen.x, handleScreen.y - originScreen.y);
        const float screenDistance = glm::length(delta);
        if (screenDistance <= 0.0001f)
            return result;

        const float worldDistance = glm::length(handleWorld - originWorld);
        if (worldDistance <= 0.000001f)
            return result;

        result.screenPos = handleScreen;
        result.axisScreenDir = delta / screenDistance;
        result.worldPerPixel = worldDistance / screenDistance;
        result.screenDistance = screenDistance;
        result.valid = true;
        return result;
    }
} // namespace

Editor::Editor()
{
    m_editorCamera = std::make_shared<engine::Camera>();
}

void Editor::drawGuizmo()
{
    m_isColliderHandleHovered = false;

    if (!m_selectedEntity)
    {
        m_isColliderHandleActive = false;
        m_activeColliderHandle = ColliderHandleType::NONE;
        return;
    }

    auto *tc = m_selectedEntity->getComponent<engine::Transform3DComponent>();
    if (!tc)
    {
        m_isColliderHandleActive = false;
        m_activeColliderHandle = ColliderHandleType::NONE;
        return;
    }

    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());

    const ImVec2 viewportPos = ImGui::GetWindowPos();
    const ImVec2 viewportSize = ImGui::GetWindowSize();

    ImGuizmo::SetRect(
        viewportPos.x,
        viewportPos.y,
        viewportSize.x,
        viewportSize.y);

    std::vector<ColliderHandleProjection> colliderHandles;
    auto *collisionComponent = m_selectedEntity->getComponent<engine::CollisionComponent>();

    if (collisionComponent && m_showCollisionBounds && m_editorCamera)
    {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const glm::mat4 view = m_editorCamera->getViewMatrix();
        const glm::mat4 projection = m_editorCamera->getProjectionMatrix();
        const glm::vec3 worldPosition = tc->getWorldPosition();
        const glm::quat worldRotation = tc->getWorldRotation();
        const glm::mat4 colliderMatrix = composeTransform(worldPosition, worldRotation) * shapeLocalPoseToMatrix(collisionComponent->getShape());

        const glm::vec3 center = transformPoint(colliderMatrix, glm::vec3(0.0f));
        const glm::vec3 axisX = transformDirection(colliderMatrix, glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::vec3 axisY = transformDirection(colliderMatrix, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 axisZ = transformDirection(colliderMatrix, glm::vec3(0.0f, 0.0f, 1.0f));

        constexpr ImU32 colliderLineColor = IM_COL32(75, 215, 255, 220);
        constexpr ImU32 boxAxisXColor = IM_COL32(245, 95, 95, 235);
        constexpr ImU32 boxAxisYColor = IM_COL32(95, 235, 120, 235);
        constexpr ImU32 boxAxisZColor = IM_COL32(110, 160, 255, 235);

        if (collisionComponent->getShapeType() == engine::CollisionComponent::ShapeType::BOX)
        {
            const glm::vec3 halfExtents = collisionComponent->getBoxHalfExtents();
            const std::array<glm::vec3, 8> localCorners = {
                glm::vec3(-halfExtents.x, -halfExtents.y, -halfExtents.z),
                glm::vec3(halfExtents.x, -halfExtents.y, -halfExtents.z),
                glm::vec3(halfExtents.x, halfExtents.y, -halfExtents.z),
                glm::vec3(-halfExtents.x, halfExtents.y, -halfExtents.z),
                glm::vec3(-halfExtents.x, -halfExtents.y, halfExtents.z),
                glm::vec3(halfExtents.x, -halfExtents.y, halfExtents.z),
                glm::vec3(halfExtents.x, halfExtents.y, halfExtents.z),
                glm::vec3(-halfExtents.x, halfExtents.y, halfExtents.z)};

            std::array<glm::vec3, 8> worldCorners{};
            for (size_t index = 0; index < localCorners.size(); ++index)
                worldCorners[index] = transformPoint(colliderMatrix, localCorners[index]);

            constexpr std::array<std::pair<int, int>, 12> edges = {
                std::pair<int, int>{0, 1}, {1, 2}, {2, 3}, {3, 0},
                {4, 5}, {5, 6}, {6, 7}, {7, 4},
                {0, 4}, {1, 5}, {2, 6}, {3, 7}};

            for (const auto &[from, to] : edges)
                drawLine3D(drawList, worldCorners[from], worldCorners[to], view, projection, viewportPos, viewportSize, colliderLineColor, 1.5f);

            const glm::vec3 handleX = center + axisX * halfExtents.x;
            const glm::vec3 handleY = center + axisY * halfExtents.y;
            const glm::vec3 handleZ = center + axisZ * halfExtents.z;

            colliderHandles.push_back(makeHandleProjection(static_cast<int>(ColliderHandleType::BOX_X), center, handleX, view, projection, viewportPos, viewportSize));
            colliderHandles.push_back(makeHandleProjection(static_cast<int>(ColliderHandleType::BOX_Y), center, handleY, view, projection, viewportPos, viewportSize));
            colliderHandles.push_back(makeHandleProjection(static_cast<int>(ColliderHandleType::BOX_Z), center, handleZ, view, projection, viewportPos, viewportSize));
        }
        else if (collisionComponent->getShapeType() == engine::CollisionComponent::ShapeType::CAPSULE)
        {
            const float radius = collisionComponent->getCapsuleRadius();
            const float halfHeight = collisionComponent->getCapsuleHalfHeight();
            const glm::vec3 topCenter = center + axisX * halfHeight;
            const glm::vec3 bottomCenter = center - axisX * halfHeight;

            auto drawCircle = [&](const glm::vec3 &circleCenter,
                                  const glm::vec3 &basisU,
                                  const glm::vec3 &basisV,
                                  float circleRadius)
            {
                constexpr int segments = 28;
                for (int segment = 0; segment < segments; ++segment)
                {
                    const float t0 = (static_cast<float>(segment) / static_cast<float>(segments)) * glm::two_pi<float>();
                    const float t1 = (static_cast<float>(segment + 1) / static_cast<float>(segments)) * glm::two_pi<float>();
                    const glm::vec3 p0 = circleCenter + (basisU * std::cos(t0) + basisV * std::sin(t0)) * circleRadius;
                    const glm::vec3 p1 = circleCenter + (basisU * std::cos(t1) + basisV * std::sin(t1)) * circleRadius;
                    drawLine3D(drawList, p0, p1, view, projection, viewportPos, viewportSize, colliderLineColor, 1.3f);
                }
            };

            drawCircle(topCenter, axisY, axisZ, radius);
            drawCircle(bottomCenter, axisY, axisZ, radius);
            drawCircle(center, axisY, axisZ, radius);

            drawLine3D(drawList, topCenter + axisY * radius, bottomCenter + axisY * radius, view, projection, viewportPos, viewportSize, colliderLineColor, 1.3f);
            drawLine3D(drawList, topCenter - axisY * radius, bottomCenter - axisY * radius, view, projection, viewportPos, viewportSize, colliderLineColor, 1.3f);
            drawLine3D(drawList, topCenter + axisZ * radius, bottomCenter + axisZ * radius, view, projection, viewportPos, viewportSize, colliderLineColor, 1.3f);
            drawLine3D(drawList, topCenter - axisZ * radius, bottomCenter - axisZ * radius, view, projection, viewportPos, viewportSize, colliderLineColor, 1.3f);

            const glm::vec3 radiusHandleY = center + axisY * radius;
            const glm::vec3 radiusHandleZ = center + axisZ * radius;
            const glm::vec3 heightHandle = center + axisX * (halfHeight + radius);

            colliderHandles.push_back(makeHandleProjection(static_cast<int>(ColliderHandleType::CAPSULE_RADIUS_Y), center, radiusHandleY, view, projection, viewportPos, viewportSize));
            colliderHandles.push_back(makeHandleProjection(static_cast<int>(ColliderHandleType::CAPSULE_RADIUS_Z), center, radiusHandleZ, view, projection, viewportPos, viewportSize));
            colliderHandles.push_back(makeHandleProjection(static_cast<int>(ColliderHandleType::CAPSULE_HEIGHT), center, heightHandle, view, projection, viewportPos, viewportSize));
        }

        int hoveredHandleId = static_cast<int>(ColliderHandleType::NONE);
        float hoveredDistanceSq = std::numeric_limits<float>::max();
        const ImVec2 mousePosition = ImGui::GetMousePos();
        const bool canEditCollider = m_enableCollisionBoundsEditing && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        if (canEditCollider && !m_isColliderHandleActive)
        {
            for (const auto &handle : colliderHandles)
            {
                if (!handle.valid)
                    continue;

                const float dx = mousePosition.x - handle.screenPos.x;
                const float dy = mousePosition.y - handle.screenPos.y;
                const float distanceSq = dx * dx + dy * dy;
                if (distanceSq < hoveredDistanceSq)
                {
                    hoveredDistanceSq = distanceSq;
                    hoveredHandleId = handle.handleId;
                }
            }

            if (hoveredDistanceSq > (12.0f * 12.0f))
                hoveredHandleId = static_cast<int>(ColliderHandleType::NONE);
        }

        m_isColliderHandleHovered = hoveredHandleId != static_cast<int>(ColliderHandleType::NONE);

        if (!m_isColliderHandleActive && m_isColliderHandleHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver())
        {
            for (const auto &handle : colliderHandles)
            {
                if (!handle.valid || handle.handleId != hoveredHandleId)
                    continue;

                m_isColliderHandleActive = true;
                m_activeColliderHandle = static_cast<ColliderHandleType>(handle.handleId);
                m_colliderDragStartMouse = glm::vec2(mousePosition.x, mousePosition.y);
                m_colliderDragAxisScreenDir = handle.axisScreenDir;
                m_colliderDragWorldPerPixel = handle.worldPerPixel;
                m_colliderDragStartBoxHalfExtents = collisionComponent->getBoxHalfExtents();
                m_colliderDragStartCapsuleRadius = collisionComponent->getCapsuleRadius();
                m_colliderDragStartCapsuleHalfHeight = collisionComponent->getCapsuleHalfHeight();
                break;
            }
        }

        if (m_isColliderHandleActive)
        {
            m_isColliderHandleHovered = true;

            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_colliderDragWorldPerPixel > 0.0f)
            {
                const glm::vec2 currentMouse(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
                const float pixelDelta = glm::dot(currentMouse - m_colliderDragStartMouse, m_colliderDragAxisScreenDir);
                const float worldDelta = pixelDelta * m_colliderDragWorldPerPixel;

                switch (m_activeColliderHandle)
                {
                case ColliderHandleType::BOX_X:
                {
                    glm::vec3 nextHalfExtents = m_colliderDragStartBoxHalfExtents;
                    nextHalfExtents.x = std::max(0.01f, nextHalfExtents.x + worldDelta);
                    collisionComponent->setBoxHalfExtents(nextHalfExtents);
                    break;
                }
                case ColliderHandleType::BOX_Y:
                {
                    glm::vec3 nextHalfExtents = m_colliderDragStartBoxHalfExtents;
                    nextHalfExtents.y = std::max(0.01f, nextHalfExtents.y + worldDelta);
                    collisionComponent->setBoxHalfExtents(nextHalfExtents);
                    break;
                }
                case ColliderHandleType::BOX_Z:
                {
                    glm::vec3 nextHalfExtents = m_colliderDragStartBoxHalfExtents;
                    nextHalfExtents.z = std::max(0.01f, nextHalfExtents.z + worldDelta);
                    collisionComponent->setBoxHalfExtents(nextHalfExtents);
                    break;
                }
                case ColliderHandleType::CAPSULE_RADIUS_Y:
                case ColliderHandleType::CAPSULE_RADIUS_Z:
                {
                    const float nextRadius = std::max(0.01f, m_colliderDragStartCapsuleRadius + worldDelta);
                    collisionComponent->setCapsuleDimensions(nextRadius, m_colliderDragStartCapsuleHalfHeight);
                    break;
                }
                case ColliderHandleType::CAPSULE_HEIGHT:
                {
                    const float nextHalfHeight = std::max(0.0f, m_colliderDragStartCapsuleHalfHeight + worldDelta);
                    collisionComponent->setCapsuleDimensions(m_colliderDragStartCapsuleRadius, nextHalfHeight);
                    break;
                }
                default:
                    break;
                }
            }
            else
            {
                m_isColliderHandleActive = false;
                m_activeColliderHandle = ColliderHandleType::NONE;
            }
        }

        for (const auto &handle : colliderHandles)
        {
            if (!handle.valid)
                continue;

            const bool isActiveHandle = m_isColliderHandleActive && static_cast<int>(m_activeColliderHandle) == handle.handleId;
            const bool isHoveredHandle = !isActiveHandle && hoveredHandleId == handle.handleId;

            ImU32 handleColor = IM_COL32(90, 220, 255, 235);
            if (isActiveHandle)
                handleColor = IM_COL32(255, 175, 60, 245);
            else if (isHoveredHandle)
                handleColor = IM_COL32(255, 235, 95, 245);

            float radius = 5.0f;
            if (handle.handleId == static_cast<int>(ColliderHandleType::BOX_X))
                handleColor = isActiveHandle ? handleColor : boxAxisXColor;
            else if (handle.handleId == static_cast<int>(ColliderHandleType::BOX_Y))
                handleColor = isActiveHandle ? handleColor : boxAxisYColor;
            else if (handle.handleId == static_cast<int>(ColliderHandleType::BOX_Z))
                handleColor = isActiveHandle ? handleColor : boxAxisZColor;

            if (isHoveredHandle || isActiveHandle)
                radius = 6.5f;

            drawList->AddCircleFilled(handle.screenPos, radius, handleColor);
        }
    }
    else
    {
        m_isColliderHandleActive = false;
        m_activeColliderHandle = ColliderHandleType::NONE;
    }

    const bool blockTransformGuizmo = m_isColliderHandleActive;

    auto model = tc->getMatrix();

    ImGuizmo::OPERATION operation = ImGuizmo::OPERATION::TRANSLATE;

    switch (m_currentGuizmoOperation)
    {
    case GuizmoOperation::TRANSLATE:
        operation = ImGuizmo::OPERATION::TRANSLATE;
        break;
    case GuizmoOperation::ROTATE:
        operation = ImGuizmo::OPERATION::ROTATE;
        break;
    case GuizmoOperation::SCALE:
        operation = ImGuizmo::OPERATION::SCALE;
        break;
    }

    if (!blockTransformGuizmo)
    {
        ImGuizmo::Manipulate(
            glm::value_ptr(m_editorCamera->getViewMatrix()),
            glm::value_ptr(m_editorCamera->getProjectionMatrix()),
            operation,
            ImGuizmo::LOCAL,
            glm::value_ptr(model));

        if (ImGuizmo::IsUsing())
        {
            glm::mat4 localMatrix = model;

            if (auto *parent = m_selectedEntity->getParent())
                if (auto *parentTransform = parent->getComponent<engine::Transform3DComponent>())
                    localMatrix = glm::inverse(parentTransform->getMatrix()) * model;

            glm::vec3 translation, rotation, scale;
            ImGuizmo::DecomposeMatrixToComponents(
                glm::value_ptr(localMatrix),
                glm::value_ptr(translation),
                glm::value_ptr(rotation),
                glm::value_ptr(scale));

            tc->setPosition(translation);
            tc->setEulerDegrees(rotation);
            tc->setScale(scale);
        }
    }
}

void Editor::initStyle()
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding = 3.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;

    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.WindowPadding = ImVec2(10, 10);
    style.CellPadding = ImVec2(6, 4);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 9.0f;

    const ImVec4 bg0 = ImVec4(0.070f, 0.073f, 0.078f, 1.000f);
    const ImVec4 bg1 = ImVec4(0.095f, 0.100f, 0.108f, 1.000f);
    const ImVec4 bg2 = ImVec4(0.120f, 0.126f, 0.136f, 1.000f);
    const ImVec4 bg3 = ImVec4(0.155f, 0.164f, 0.176f, 1.000f);

    const ImVec4 accentBlue = ImVec4(0.120f, 0.420f, 0.900f, 1.000f);
    const ImVec4 accentBlueHover = ImVec4(0.180f, 0.500f, 0.950f, 1.000f);
    const ImVec4 accentBlueActive = ImVec4(0.090f, 0.350f, 0.780f, 1.000f);
    const ImVec4 accentOrange = ImVec4(0.930f, 0.490f, 0.130f, 1.000f);

    colors[ImGuiCol_Text] = ImVec4(0.880f, 0.900f, 0.930f, 1.000f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.520f, 0.560f, 0.620f, 1.000f);
    colors[ImGuiCol_WindowBg] = bg0;
    colors[ImGuiCol_ChildBg] = bg1;
    colors[ImGuiCol_PopupBg] = bg1;
    colors[ImGuiCol_Border] = ImVec4(0.245f, 0.255f, 0.280f, 0.900f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.000f, 0.000f, 0.000f, 0.000f);

    colors[ImGuiCol_FrameBg] = bg2;
    colors[ImGuiCol_FrameBgHovered] = bg3;
    colors[ImGuiCol_FrameBgActive] = accentBlueActive;

    colors[ImGuiCol_TitleBg] = bg0;
    colors[ImGuiCol_TitleBgActive] = bg1;
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(bg0.x, bg0.y, bg0.z, 0.700f);
    colors[ImGuiCol_MenuBarBg] = bg1;

    colors[ImGuiCol_ScrollbarBg] = ImVec4(bg0.x, bg0.y, bg0.z, 0.800f);
    colors[ImGuiCol_ScrollbarGrab] = bg3;
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.250f, 0.265f, 0.290f, 1.000f);
    colors[ImGuiCol_ScrollbarGrabActive] = accentBlue;

    colors[ImGuiCol_CheckMark] = accentBlueHover;
    colors[ImGuiCol_SliderGrab] = accentBlue;
    colors[ImGuiCol_SliderGrabActive] = accentBlueHover;

    colors[ImGuiCol_Button] = bg2;
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.195f, 0.206f, 0.222f, 1.000f);
    colors[ImGuiCol_ButtonActive] = accentBlueActive;

    colors[ImGuiCol_Header] = bg2;
    colors[ImGuiCol_HeaderHovered] = bg3;
    colors[ImGuiCol_HeaderActive] = accentBlueActive;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.220f, 0.230f, 0.250f, 0.600f);
    colors[ImGuiCol_ResizeGripHovered] = accentBlue;
    colors[ImGuiCol_ResizeGripActive] = accentBlueHover;

    colors[ImGuiCol_Tab] = bg1;
    colors[ImGuiCol_TabHovered] = ImVec4(0.190f, 0.205f, 0.240f, 1.000f);
    colors[ImGuiCol_TabActive] = bg2;
    colors[ImGuiCol_TabUnfocused] = bg1;
    colors[ImGuiCol_TabUnfocusedActive] = bg2;

    colors[ImGuiCol_TextSelectedBg] = ImVec4(accentBlue.x, accentBlue.y, accentBlue.z, 0.400f);
    colors[ImGuiCol_DragDropTarget] = accentOrange;
    colors[ImGuiCol_NavHighlight] = accentBlue;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.000f, 1.000f, 1.000f, 0.650f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.060f, 0.065f, 0.072f, 0.300f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.060f, 0.065f, 0.072f, 0.750f);

    colors[ImGuiCol_Separator] = ImVec4(0.235f, 0.245f, 0.270f, 0.800f);
    colors[ImGuiCol_SeparatorHovered] = accentBlue;
    colors[ImGuiCol_SeparatorActive] = accentBlueHover;

    colors[ImGuiCol_TableHeaderBg] = bg2;
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.260f, 0.275f, 0.300f, 1.000f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.180f, 0.192f, 0.210f, 0.600f);
    colors[ImGuiCol_TableRowBg] = bg1;
    colors[ImGuiCol_TableRowBgAlt] = bg2;

    colors[ImGuiCol_PlotLines] = accentBlue;
    colors[ImGuiCol_PlotLinesHovered] = accentBlueHover;
    colors[ImGuiCol_PlotHistogram] = accentBlue;
    colors[ImGuiCol_PlotHistogramHovered] = accentBlueHover;

    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF("./resources/fonts/JetBrainsMono-Regular.ttf", 16.0f);

    m_resourceStorage.loadNeededResources();

    m_assetsWindow = std::make_shared<AssetsWindow>(&m_resourceStorage, m_assetsPreviewSystem);
    m_assetsWindow->setOnMaterialOpenRequest([this](const std::filesystem::path &path)
                                             { openMaterialEditor(path); });
    m_assetsWindow->setOnTextAssetOpenRequest([this](const std::filesystem::path &path)
                                              { openTextDocument(path); });
    m_assetsWindow->setOnAssetSelectionChanged([this](const std::filesystem::path &path)
                                               {
                                                   m_selectedAssetPath = path;
                                                   invalidateModelDetailsCache();
                                                   if (m_selectedAssetPath.empty())
                                                       return;

                                                   m_detailsContext = DetailsContext::Asset; });

    m_entityIdBuffer = core::Buffer::createShared(sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                  core::memory::MemoryUsage::CPU_TO_GPU);

    auto &window = core::VulkanContext::getContext()->getSwapchain()->getWindow();
    GLFWwindow *windowHandler = window.getRawHandler();

    // glfwSetWindowAttrib(windowHandler, GLFW_DECORATED, !m_isDockingWindowFullscreen);
    if (m_isDockingWindowFullscreen)
    {
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        glfwSetWindowPos(windowHandler, 0, 0);
        glfwSetWindowSize(windowHandler, mode->width, mode->height);
    }

    m_defaultSampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    m_textEditor.SetPalette(TextEditor::GetDarkPalette());
    m_textEditor.SetShowWhitespaces(false);
}

void Editor::addOnModeChangedCallback(const std::function<void(EditorMode)> &function)
{
    m_onModeChangedCallbacks.push_back(function);
}

void Editor::showDockSpace()
{
    static bool dockspaceOpen = true;
    constexpr static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking;

    if (m_isDockingWindowFullscreen)
    {
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    }

    ImGui::Begin("Velix dockSpace", &dockspaceOpen, windowFlags);

    if (m_isDockingWindowFullscreen)
        ImGui::PopStyleVar(2);

    ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
    m_dockSpaceId = dockspaceId;
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);

    ImGui::End();

    static bool first_time = true;

    if (first_time)
    {
        first_time = false;

        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

        ImGuiID dockMainId = dockspaceId;

        ImGuiID titleBarDock = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Up, 0.04f, nullptr, &dockMainId);
        ImGui::DockBuilderDockWindow("TitleBar", titleBarDock);

        ImGuiID toolbarDock = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Up, 0.03f, nullptr, &dockMainId);
        ImGui::DockBuilderDockWindow("Toolbar", toolbarDock);

        ImGuiID bottomStripDock = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.04f, nullptr, &dockMainId);
        ImGui::DockBuilderDockWindow("BottomPanel", bottomStripDock);

        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 0.20f, nullptr, &dockMainId);

        ImGuiID dockRightTop = dockRight;
        ImGuiID dockRightBottom = ImGui::DockBuilderSplitNode(dockRightTop, ImGuiDir_Down, 0.5f, nullptr, &dockRightTop);

        ImGui::DockBuilderDockWindow("Hierarchy", dockRightTop);
        ImGui::DockBuilderDockWindow("Details", dockRightBottom);

        ImGuiID assetsDock = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 0.25f, nullptr, &dockMainId);
        m_assetsPanelsDockId = assetsDock;
        ImGui::DockBuilderDockWindow("Assets", assetsDock);

        ImGui::DockBuilderDockWindow("Viewport", dockMainId);
        m_centerDockId = dockMainId;
        ImGui::DockBuilderFinish(dockspaceId);

        if (ImGuiDockNode *node = ImGui::DockBuilderGetNode(bottomStripDock))
        {
            node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
            node->LocalFlags |= ImGuiDockNodeFlags_NoSplit;
            node->LocalFlags |= ImGuiDockNodeFlags_NoResize;
            node->LocalFlags |= ImGuiDockNodeFlags_NoDockingOverMe;
        }
    }
}

void Editor::syncAssetsAndTerminalDocking()
{
    if (m_dockSpaceId == 0 || m_assetsPanelsDockId == 0)
        return;

    const bool assetsVisible = m_showAssetsWindow;
    const bool terminalVisible = m_showTerminal;

    if (assetsVisible == m_lastDockedAssetsVisibility && terminalVisible == m_lastDockedTerminalVisibility)
        return;

    m_lastDockedAssetsVisibility = assetsVisible;
    m_lastDockedTerminalVisibility = terminalVisible;

    if (!ImGui::DockBuilderGetNode(m_assetsPanelsDockId))
        return;

    ImGui::DockBuilderRemoveNodeChildNodes(m_assetsPanelsDockId);

    if (assetsVisible && terminalVisible)
    {
        ImGuiID leftNode = m_assetsPanelsDockId;
        ImGuiID rightNode = ImGui::DockBuilderSplitNode(leftNode, ImGuiDir_Right, 0.5f, nullptr, &leftNode);

        ImGui::DockBuilderDockWindow("Assets", leftNode);
        ImGui::DockBuilderDockWindow("Terminal with logs", rightNode);
    }
    else if (assetsVisible)
        ImGui::DockBuilderDockWindow("Assets", m_assetsPanelsDockId);
    else if (terminalVisible)
        ImGui::DockBuilderDockWindow("Terminal with logs", m_assetsPanelsDockId);

    ImGui::DockBuilderFinish(m_dockSpaceId);
}

void Editor::drawCustomTitleBar()
{
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGuiWindowClass windowClass;
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
    ImGui::SetNextWindowClass(&windowClass);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::Begin("TitleBar", nullptr, windowFlags);

    ImGuiIO &io = ImGui::GetIO();
    ImGuiStyle &style = ImGui::GetStyle();

    ImGui::SetCursorPos(ImVec2(4, (ImGui::GetWindowHeight() - 30) * 0.5f));
    ImVec2 logoSize = ImVec2(50, 30);
    ImGui::Image(m_resourceStorage.getTextureDescriptorSet("./resources/textures/VelixFire.tex.elixasset"), logoSize);

    ImGui::SameLine(0, 10);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

    if (ImGui::Button("File"))
    {
        if (ImGui::IsPopupOpen("FilePopup"))
            ImGui::CloseCurrentPopup();
        else
            ImGui::OpenPopup("FilePopup");
    }

    ImGui::SameLine(0, 10);

    if (ImGui::Button("Tools"))
    {
        if (ImGui::IsPopupOpen("CreateNewClassPopup"))
            ImGui::CloseCurrentPopup();
        else
            ImGui::OpenPopup("CreateNewClassPopup");
    }

    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar();

    // ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui:s:GetItemRectMin().y + ImGui::GetItemRectSize().y));

    if (ImGui::BeginPopup("CreateNewClassPopup"))
    {
        // ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        if (ImGui::Button("Create new C++ class"))
        {
            if (ImGui::IsPopupOpen("CreateNewClass"))
                ImGui::CloseCurrentPopup();
            else
                ImGui::OpenPopup("CreateNewClass");
        }

        if (ImGui::BeginPopup("CreateNewClass"))
        {
            static char newClassName[256];

            ImGui::InputTextWithHint("##CreateClass", "New c++ class name...", newClassName, sizeof(newClassName));
            ImGui::Separator();

            if (ImGui::Button("Create"))
            {
                std::ifstream hppFileTemplate("./resources/scripts_template/ScriptTemplate.hpp.txt");
                std::ifstream cppFileTemplate("./resources/scripts_template/ScriptTemplate.cpp.txt");

                std::string hppString(std::istreambuf_iterator<char>{hppFileTemplate}, {});
                std::string cppString(std::istreambuf_iterator<char>{cppFileTemplate}, {});

                hppFileTemplate.close();
                cppFileTemplate.close();

                std::size_t pos = 0;

                std::string token = "{{ClassName}}";
                std::string className(newClassName);

                while ((pos = hppString.find(token, pos)) != std::string::npos)
                {
                    hppString.replace(pos, token.length(), className);
                    pos += className.length();
                }

                pos = 0;

                while ((pos = cppString.find(token, pos)) != std::string::npos)
                {
                    cppString.replace(pos, token.length(), className);
                    pos += className.length();
                }

                std::string sourceFolder = m_currentProject.lock()->sourcesDir;

                if (sourceFolder.back() != '/')
                    sourceFolder += '/';

                std::ofstream hppCreateFile(sourceFolder + className + ".hpp");
                std::ofstream cppCreateFile(sourceFolder + className + ".cpp");

                hppCreateFile << hppString << std::endl;
                cppCreateFile << cppString << std::endl;

                hppCreateFile.close();
                cppCreateFile.close();

                ImGui::CloseCurrentPopup();

                m_notificationManager.showInfo("Successfully created a new c++ class");
                VX_EDITOR_INFO_STREAM("Created new script class: " << className);
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();

            // ImGui::PopStyleColor(1);
            ImGui::EndPopup();
        }

        if (ImGui::Button("Build Project"))
        {
            auto project = m_currentProject.lock();
            if (!project)
            {
                VX_EDITOR_ERROR_STREAM("Build aborted: project is not loaded\n");
                m_notificationManager.showError("Build failed: no project loaded");
            }
            else
            {
                const std::filesystem::path projectRoot = project->fullPath;
                if (projectRoot.empty() || !std::filesystem::exists(projectRoot))
                {
                    VX_EDITOR_ERROR_STREAM("Build aborted: invalid project path '" << project->fullPath << "'\n");
                    m_notificationManager.showError("Build failed: invalid project path");
                }
                else
                {
                    const std::filesystem::path buildDirectory = project->buildDir.empty() ? projectRoot / "build"
                                                                                           : std::filesystem::path(project->buildDir);
                    std::filesystem::path cmakePrefixPath = FileHelper::getExecutablePath();
                    if (cmakePrefixPath.filename() == "bin")
                        cmakePrefixPath = cmakePrefixPath.parent_path();

                    const std::filesystem::path cmakeExecutablePath = resolveCMakeExecutablePath();
                    const std::string cmakeCommandToken = makeExecutableCommandToken(cmakeExecutablePath);

                    if (cmakeExecutablePath.is_absolute())
                        VX_EDITOR_INFO_STREAM("Using bundled CMake executable: " << cmakeExecutablePath << '\n');
                    else
                        VX_EDITOR_INFO_STREAM("Using CMake from PATH: " << cmakeExecutablePath.string() << '\n');

                    const std::string configureCommand = cmakeCommandToken + " -S " + quoteShellArgument(projectRoot) +
                                                         " -B " + quoteShellArgument(buildDirectory) +
                                                         " -DCMAKE_PREFIX_PATH=" + quoteShellArgument(cmakePrefixPath) +
                                                         " -DCMAKE_BUILD_TYPE=Release" +
                                                         " -DCMAKE_EXPORT_COMPILE_COMMANDS=ON";

                    const auto [configureResult, configureOutput] = FileHelper::executeCommand(configureCommand);
                    if (configureResult != 0)
                    {
                        VX_EDITOR_ERROR_STREAM("CMake configure failed\n"
                                               << configureOutput << '\n');
                        m_notificationManager.showError("Build failed: cmake configure error");
                    }
                    else
                    {
                        if (!syncProjectCompileCommands(*project))
                            VX_EDITOR_WARNING_STREAM("Failed to sync compile_commands.json to project root\n");

                        std::string buildCommand = cmakeCommandToken + " --build " + quoteShellArgument(buildDirectory) + " --config Release";
#if defined(__linux__)
                        buildCommand += " -j";
#endif

                        const auto [buildResult, buildOutput] = FileHelper::executeCommand(buildCommand);
                        if (buildResult != 0)
                        {
                            VX_EDITOR_ERROR_STREAM("Project build failed\n"
                                                   << buildOutput << '\n');
                            m_notificationManager.showError("Build failed");
                        }
                        else
                        {
                            const auto moduleLibraryPath = findGameModuleLibraryPath(buildDirectory);
                            if (moduleLibraryPath.empty())
                            {
                                VX_EDITOR_ERROR_STREAM("Build succeeded but GameModule library was not found in " << buildDirectory << '\n');
                                m_notificationManager.showError("Build succeeded, but GameModule was not found");
                            }
                            else
                            {
                                bool hasAttachedScriptComponents = false;
                                if (m_scene)
                                {
                                    for (const auto &entity : m_scene->getEntities())
                                    {
                                        if (entity->getComponents<engine::ScriptComponent>().empty())
                                            continue;

                                        hasAttachedScriptComponents = true;
                                        break;
                                    }
                                }

                                if (project->projectLibrary && hasAttachedScriptComponents)
                                {
                                    VX_EDITOR_WARNING_STREAM("Skipping module reload because script components are attached in current scene\n");
                                    m_notificationManager.showWarning("Build done. Stop Play/remove scripts to reload module.");
                                }
                                else
                                {
                                    if (project->projectLibrary)
                                    {
                                        engine::PluginLoader::closeLibrary(project->projectLibrary);
                                        project->projectLibrary = nullptr;
                                        m_projectScriptsRegister = nullptr;
                                        m_loadedGameModulePath.clear();
                                    }

                                    project->projectLibrary = engine::PluginLoader::loadLibrary(moduleLibraryPath.string());
                                    if (!project->projectLibrary)
                                    {
                                        m_projectScriptsRegister = nullptr;
                                        m_loadedGameModulePath.clear();
                                        VX_EDITOR_ERROR_STREAM("Failed to load game module: " << moduleLibraryPath << '\n');
                                        m_notificationManager.showError("Build done, but failed to load module");
                                    }
                                    else
                                    {
                                        auto getScriptsRegisterFunction = engine::PluginLoader::getFunction<engine::ScriptsRegister &(*)()>("getScriptsRegister", project->projectLibrary);
                                        if (!getScriptsRegisterFunction)
                                        {
                                            engine::PluginLoader::closeLibrary(project->projectLibrary);
                                            project->projectLibrary = nullptr;

                                            m_projectScriptsRegister = nullptr;
                                            m_loadedGameModulePath.clear();

                                            VX_EDITOR_ERROR_STREAM("Module loaded but getScriptsRegister was not found: " << moduleLibraryPath << '\n');
                                            m_notificationManager.showError("Module loaded, but script register function is missing");
                                        }
                                        else
                                        {
                                            m_projectScriptsRegister = &getScriptsRegisterFunction();
                                            m_loadedGameModulePath = moduleLibraryPath.string();

                                            const std::size_t scriptsCount = m_projectScriptsRegister->getScripts().size();
                                            VX_EDITOR_INFO_STREAM("Loaded " << scriptsCount << " script(s) from " << m_loadedGameModulePath << '\n');
                                            m_notificationManager.showSuccess("Build done. Loaded scripts: " + std::to_string(scriptsCount));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        ImGui::Separator();

        auto &engineConfig = engine::EngineConfig::instance();
        const auto &detectedIdes = engineConfig.getDetectedIdes();

        if (ImGui::Button("Refresh IDE Detection"))
        {
            if (engineConfig.reload())
                m_notificationManager.showInfo("IDE detection refreshed");
            else
                m_notificationManager.showWarning("IDE detection refreshed with errors");
        }

        ImGui::TextWrapped("Detected IDEs: %s", joinDetectedIdeNames(detectedIdes).c_str());

        if (!detectedIdes.empty())
        {
            std::string currentPreferredIdeName = "None";
            if (auto currentPreferredIde = engineConfig.findIde(engineConfig.getPreferredIdeId()))
                currentPreferredIdeName = currentPreferredIde->displayName;

            if (ImGui::BeginCombo("Preferred IDE", currentPreferredIdeName.c_str()))
            {
                for (const auto &ide : detectedIdes)
                {
                    const bool isSelected = ide.id == engineConfig.getPreferredIdeId();
                    if (ImGui::Selectable(ide.displayName.c_str(), isSelected))
                    {
                        engineConfig.setPreferredIdeId(ide.id);
                        if (!engineConfig.save())
                            m_notificationManager.showWarning("Failed to persist preferred IDE");
                    }

                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        auto project = m_currentProject.lock();
        const auto preferredVSCode = engineConfig.findPreferredVSCodeIde();
        const bool hasProject = project && !project->fullPath.empty() && std::filesystem::exists(project->fullPath);
        const bool canOpenProjectInVSCode = hasProject && preferredVSCode.has_value();

        if (!preferredVSCode)
            ImGui::TextDisabled("VSCode was not found in PATH (code/code-insiders/codium).");

        if (!hasProject)
            ImGui::TextDisabled("No loaded project to open.");

        if (!canOpenProjectInVSCode)
            ImGui::BeginDisabled();

        if (ImGui::Button("Open Project in VSCode"))
        {
            std::filesystem::path projectRoot = project->fullPath;
            const std::string command = quoteShellArgument(preferredVSCode->command) + " " + quoteShellArgument(projectRoot);

            if (!syncProjectCompileCommands(*project))
                VX_EDITOR_WARNING_STREAM("compile_commands.json was not found in build directory before opening VSCode\n");

            if (FileHelper::launchDetachedCommand(command))
            {
                VX_EDITOR_INFO_STREAM("Opened project in VSCode using '" << preferredVSCode->command << "': " << projectRoot << '\n');
                m_notificationManager.showSuccess("Opened project in VSCode");
            }
            else
            {
                VX_EDITOR_ERROR_STREAM("Failed to open project in VSCode\n");
                m_notificationManager.showError("Failed to open project in VSCode");
            }
        }

        if (!canOpenProjectInVSCode)
            ImGui::EndDisabled();

        ImGui::EndPopup();

        // ImGui::PopStyleColor(1);
    }

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y + ImGui::GetItemRectSize().y));
    auto &window = core::VulkanContext::getContext()->getSwapchain()->getWindow();
    GLFWwindow *windowHandler = window.getRawHandler();

    if (ImGui::BeginPopup("FilePopup"))
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        if (ImGui::Button("New scene"))
        {
            if (ImGui::IsPopupOpen("CreateNewScene"))
                ImGui::CloseCurrentPopup();
            else
                ImGui::OpenPopup("CreateNewScene");
        }

        if (ImGui::BeginPopup("CreateNewScene"))
        {
            // ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            static char sceneBuffer[256];
            ImGui::InputTextWithHint("##NewScene", "New scene name...", sceneBuffer, sizeof(sceneBuffer));
            ImGui::Separator();

            ImGui::Button("Create");

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();

            // ImGui::PopStyleColor(1);
            ImGui::EndPopup();
        }

        if (ImGui::Button("Open scene"))
        {
            // TODO open my own file editor
        }
        if (ImGui::Button("Save"))
        {
            auto project = m_currentProject.lock();
            if (m_scene && project)
            {
                m_scene->saveSceneToFile(project->entryScene);
                m_notificationManager.showInfo("Scene saved");
                VX_EDITOR_INFO_STREAM("Scene saved to: " << project->entryScene);
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Exit"))
            window.close(); // Ha-ha-ha-ha Kill it slower dumbass
        ImGui::PopStyleColor(1);

        ImGui::EndPopup();
    }

    ImGui::SameLine();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

    if (ImGui::Button("Edit"))
    {
        if (ImGui::IsPopupOpen("EditPopup"))
            ImGui::CloseCurrentPopup();
        else
            ImGui::OpenPopup("EditPopup");
    }

    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar();

    if (ImGui::BeginPopup("EditPopup"))
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        ImGui::Button("Undo Ctrl+Z");
        ImGui::Button("Redo Ctrl+Y");

        ImGui::PopStyleColor(1);
        ImGui::EndPopup();
    }

    float windowWidth = ImGui::GetWindowWidth();
    float buttonSize = ImGui::GetFrameHeight();
    ImGui::SameLine(windowWidth - buttonSize * 3 - 30);

    if (ImGui::Button("_", ImVec2(buttonSize, buttonSize * 0.9f)))
        window.iconify();

    ImGui::SameLine();

    if (ImGui::Button("[]", ImVec2(buttonSize, buttonSize * 0.9f)))
        m_isDockingWindowFullscreen = !m_isDockingWindowFullscreen;

    ImGui::SameLine();

    if (ImGui::Button("X", ImVec2(buttonSize, buttonSize * 0.9f)))
        window.close();

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::End();

    ImGui::PopStyleVar(2);
}

void Editor::changeMode(EditorMode mode)
{
    if (m_currentMode == mode)
        return;

    m_currentMode = mode;

    switch (mode)
    {
    case EditorMode::EDIT:
        VX_EDITOR_INFO_STREAM("Switched editor mode to EDIT");
        m_notificationManager.showInfo("Mode: Edit");
        break;
    case EditorMode::PLAY:
        VX_EDITOR_INFO_STREAM("Switched editor mode to PLAY");
        m_notificationManager.showSuccess("Mode: Play");
        break;
    case EditorMode::PAUSE:
        VX_EDITOR_INFO_STREAM("Switched editor mode to PAUSE");
        m_notificationManager.showWarning("Mode: Pause");
        break;
    }

    for (const auto &callback : m_onModeChangedCallbacks)
        if (callback)
            callback(mode);
}

void Editor::drawToolBar()
{
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    static bool showBenchmark = false;

    ImGuiWindowClass windowClass;
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
    ImGui::SetNextWindowClass(&windowClass);

    ImGui::Begin("Toolbar", nullptr, windowFlags);

    if (ImGui::BeginMenuBar())
    {
        // TODO make it better
        const std::vector<std::string> selectionModes = {"Translate", "Rotate", "Scale"};
        static int guizmoMode = 0;

        switch (m_currentGuizmoOperation)
        {
        case GuizmoOperation::TRANSLATE:
            guizmoMode = 0;
            break;
        case GuizmoOperation::ROTATE:
            guizmoMode = 1;
            break;
        case GuizmoOperation::SCALE:
            guizmoMode = 2;
            break;
        }

        ImGui::PushItemWidth(120.0f);
        // ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(50, 200));
        if (ImGui::BeginCombo("##Selection mode", selectionModes[guizmoMode].c_str()))
        {
            for (int i = 0; i < selectionModes.size(); ++i)
            {
                const bool isSelected = (guizmoMode == i);

                if (ImGui::Selectable(selectionModes[i].c_str(), isSelected))
                {
                    guizmoMode = i;

                    if (guizmoMode == 0)
                        m_currentGuizmoOperation = GuizmoOperation::TRANSLATE;
                    else if (guizmoMode == 1)
                        m_currentGuizmoOperation = GuizmoOperation::ROTATE;
                    else if (guizmoMode == 2)
                        m_currentGuizmoOperation = GuizmoOperation::SCALE;
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        // ImGui::SameLine(200);
        ImGui::SameLine();

        std::string playText;

        if (m_currentMode == EditorMode::PAUSE || m_currentMode == EditorMode::EDIT)
            playText = "Play";

        else if (m_currentMode == EditorMode::PLAY)
            playText = "Pause";

        if (ImGui::Button(playText.c_str()))
        {
            if (m_currentMode == Editor::EDIT || m_currentMode == EditorMode::PAUSE)
            {
                changeMode(EditorMode::PLAY);
            }
            else if (m_currentMode == EditorMode::PLAY)
            {
                changeMode(EditorMode::PAUSE);
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Stop"))
        {
            changeMode(EditorMode::EDIT);
        }

        if (ImGui::Button("Benchmark"))
        {
            if (ImGui::IsPopupOpen("BenchmarkPopup"))
                ImGui::CloseCurrentPopup();
            else
                ImGui::OpenPopup("BenchmarkPopup");
        }

        ImVec2 buttonPos = ImGui::GetItemRectMin();
        ImVec2 buttonSize = ImGui::GetItemRectSize();
        ImGui::SetNextWindowPos(ImVec2(buttonPos.x, buttonPos.y + buttonSize.y));

        if (ImGui::BeginPopup("BenchmarkPopup"))
        {
            float fps = ImGui::GetIO().Framerate;
            ImGui::Text("FPS: %.1f", fps);
            ImGui::Text("Frame time: %.3f ms", 1000.0f / fps);
            ImGui::Text("VRAM usage: %ld mB", core::VulkanContext::getContext()->getDevice()->getTotalAllocatedVRAM());
            ImGui::Text("RAM usage: %ld mB", core::VulkanContext::getContext()->getDevice()->getTotalUsedRAM());

            ImGui::Separator();
            ImGui::Text("Render Graph (frame #%llu)", static_cast<unsigned long long>(m_renderGraphProfilingData.frameIndex));
            ImGui::Text("Total draw calls: %u", m_renderGraphProfilingData.totalDrawCalls);
            ImGui::Text("Total CPU frame time: %.3f ms", m_renderGraphProfilingData.cpuFrameTimeMs);
            ImGui::Text("Total CPU pass time: %.3f ms", m_renderGraphProfilingData.cpuTotalTimeMs);
            ImGui::Text("CPU wait (fence): %.3f ms", m_renderGraphProfilingData.cpuWaitForFenceMs);
            ImGui::Text("CPU wait (acquire): %.3f ms", m_renderGraphProfilingData.cpuAcquireImageMs);
            ImGui::Text("CPU submit: %.3f ms", m_renderGraphProfilingData.cpuSubmitMs);
            ImGui::Text("CPU wait (present): %.3f ms", m_renderGraphProfilingData.cpuPresentMs);
            ImGui::Text("CPU sync total: %.3f ms", m_renderGraphProfilingData.cpuSyncTimeMs);
            ImGui::Text("CPU recompile: %.3f ms", m_renderGraphProfilingData.cpuRecompileMs);
            ImGui::Text("CPU unaccounted: %.3f ms", m_renderGraphProfilingData.cpuWasteTimeMs);

            if (m_renderGraphProfilingData.gpuTimingAvailable)
            {
                ImGui::Text("Total GPU frame time: %.3f ms", m_renderGraphProfilingData.gpuFrameTimeMs);
                ImGui::Text("Total GPU pass time: %.3f ms", m_renderGraphProfilingData.gpuTotalTimeMs);
                ImGui::Text("GPU waste (non-pass): %.3f ms", m_renderGraphProfilingData.gpuWasteTimeMs);
            }
            else
                ImGui::TextDisabled("GPU timing unavailable on this GPU/queue");

            if (ImGui::BeginTable("RenderGraphPassStats", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Pass");
                ImGui::TableSetupColumn("Exec");
                ImGui::TableSetupColumn("Draws");
                ImGui::TableSetupColumn("CPU (ms)");
                ImGui::TableSetupColumn("GPU (ms)");
                ImGui::TableHeadersRow();

                for (const auto &passData : m_renderGraphProfilingData.passes)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(passData.passName.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%u", passData.executions);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%u", passData.drawCalls);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.3f", passData.cpuTimeMs);

                    ImGui::TableSetColumnIndex(4);
                    if (m_renderGraphProfilingData.gpuTimingAvailable)
                        ImGui::Text("%.3f", passData.gpuTimeMs);
                    else
                        ImGui::TextUnformatted("-");
                }

                ImGui::EndTable();
            }

            ImGui::EndPopup();
        }

        ImGui::EndMenuBar();
    }

    ImGui::End();
}

void Editor::drawBottomPanel()
{
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoTitleBar;

    ImGuiWindowClass windowClass;
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
    ImGui::SetNextWindowClass(&windowClass);

    ImGui::Begin("BottomPanel", nullptr, flags);

    if (ImGui::Button("Assets"))
        m_showAssetsWindow = !m_showAssetsWindow;

    ImGui::SameLine();

    if (ImGui::Button("Terminal with logs"))
        m_showTerminal = !m_showTerminal;

    ImGui::End();
}

void Editor::drawFrame(VkDescriptorSet viewportDescriptorSet)
{
    m_assetsPreviewSystem.beginFrame();

    handleInput();

    showDockSpace();
    syncAssetsAndTerminalDocking();

    drawCustomTitleBar();
    drawToolBar();

    if (viewportDescriptorSet)
        drawViewport(viewportDescriptorSet);

    drawMaterialEditors();

    drawDocument();
    drawAssets();
    drawTerminal();
    drawBottomPanel();
    drawHierarchy();
    drawDetails();

    m_notificationManager.render();
}

void Editor::updateAnimationPreview(float deltaTime)
{
    if (!m_scene || m_currentMode != EditorMode::EDIT)
        return;

    for (const auto &entity : m_scene->getEntities())
    {
        auto *animatorComponent = entity->getComponent<engine::AnimatorComponent>();
        if (!animatorComponent)
            continue;

        animatorComponent->update(deltaTime);
    }
}

static bool drawMaterialTextureSlot(
    const char *slotLabel,
    const std::string &currentTexturePath,
    AssetsPreviewSystem &previewSystem,
    ImVec2 thumbSize,
    bool &openPopupRequest)
{
    bool clicked = false;

    ImGui::PushID(slotLabel);

    auto ds = previewSystem.getOrRequestTexturePreview(currentTexturePath, nullptr);
    ImTextureID texId = (ImTextureID)(uintptr_t)ds;

    if (ImGui::ImageButton(slotLabel, texId, thumbSize))
    {
        openPopupRequest = true;
        clicked = true;
    }

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::TextUnformatted(slotLabel);

    std::string fileName = currentTexturePath.empty()
                               ? std::string("<None>")
                               : std::filesystem::path(currentTexturePath).filename().string();

    ImGui::TextDisabled("%s", fileName.c_str());

    if (ImGui::Button("Clear"))
    {
        // Caller should interpret empty path as "use default"
        clicked = true;
        // We signal clear by popupRequest=false and caller checks button click separately if needed.
        // If you want explicit behavior, split return enum.
    }
    ImGui::EndGroup();

    // Drag-drop support (optional but very useful)
    if (ImGui::BeginDragDropTarget())
    {
        // Example payload type, adapt to your engine
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            const char *droppedPath = static_cast<const char *>(payload->Data);
            // Caller should handle the actual assignment, this helper only draws.
            // You can redesign helper to output selected path if you want.
            (void)droppedPath;
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::PopID();
    return clicked;
}

void Editor::drawMaterialEditors()
{
    auto project = m_currentProject.lock();
    if (!project)
        return;

    const std::filesystem::path projectRoot = std::filesystem::path(project->fullPath);

    for (auto it = m_openMaterialEditors.begin(); it != m_openMaterialEditors.end();)
    {
        auto &matEditor = *it;
        const std::string matPath = matEditor.path.string();

        std::string title = matEditor.path.filename().string();
        if (matEditor.dirty)
            title += "*";

        std::string windowName = title + "###MaterialEditor_" + matPath;

        if (m_centerDockId != 0)
            ImGui::SetNextWindowDockID(m_centerDockId, ImGuiCond_FirstUseEver);

        bool keepOpen = matEditor.open;

        if (ImGui::Begin(windowName.c_str(), &keepOpen))
        {
            if (project->cache.materialsByPath.find(matPath) == project->cache.materialsByPath.end())
            {
                m_assetsPreviewSystem.getOrRequestMaterialPreview(matPath);
                ImGui::TextDisabled("Loading material...");
                ImGui::End();
                matEditor.open = keepOpen;
                if (!matEditor.open)
                    it = m_openMaterialEditors.erase(it);
                else
                    ++it;
                continue;
            }

            auto &materialAsset = project->cache.materialsByPath[matPath];
            auto gpuMat = materialAsset.gpu;
            auto &cpuMat = materialAsset.cpuData;

            auto &ui = m_materialEditorUiState[matPath];
            const bool isMaterialEditorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            const bool isCtrlDown = ImGui::GetIO().KeyCtrl;

            auto saveCurrentMaterial = [&]()
            {
                if (saveMaterialToDisk(matEditor.path, cpuMat))
                {
                    matEditor.dirty = false;
                    m_notificationManager.showSuccess("Material saved");
                    VX_EDITOR_INFO_STREAM("Material saved: " << matPath);
                }
                else
                {
                    m_notificationManager.showError("Failed to save material");
                    VX_EDITOR_ERROR_STREAM("Failed to save material: " << matPath);
                }
            };

            if (ImGui::Button("Save"))
            {
                saveCurrentMaterial();
            }
            ImGui::SameLine();

            if (ImGui::Button("Revert"))
            {
                if (reloadMaterialFromDisk(matEditor.path))
                {
                    matEditor.dirty = false;
                    m_notificationManager.showInfo("Material reloaded from disk");
                    VX_EDITOR_INFO_STREAM("Material reloaded from disk: " << matPath);
                }
                else
                {
                    m_notificationManager.showError("Failed to reload material from disk");
                    VX_EDITOR_ERROR_STREAM("Failed to reload material from disk: " << matPath);
                }
            }
            ImGui::SameLine();

            ImGui::TextDisabled("%s", matPath.c_str());

            if (isMaterialEditorFocused && isCtrlDown && ImGui::IsKeyPressed(ImGuiKey_S, false))
                saveCurrentMaterial();

            ImGui::Separator();

            if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto previewDS = m_assetsPreviewSystem.getOrRequestMaterialPreview(matPath);
                ImTextureID previewId = (ImTextureID)(uintptr_t)previewDS;

                ImGui::Image(previewId, ImVec2(160, 160));
                ImGui::SameLine();

                ImGui::BeginGroup();
                ImGui::Text("Material Instance");
                ImGui::Spacing();
                ImGui::Text("Shader: PBR Forward");
                ImGui::Text("Blend: %s",
                            (gpuMat->params().flags & engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK) ? "Alpha Blend" : (gpuMat->params().flags & engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND) ? "Masked"
                                                                                                                                                                                                                           : "Opaque");
                ImGui::EndGroup();
            }

            if (ImGui::CollapsingHeader("Surface"))
            {
                auto p = gpuMat->params();

                glm::vec4 baseColor = p.baseColorFactor;
                if (ImGui::ColorEdit4("Base Color", glm::value_ptr(baseColor)))
                {
                    gpuMat->setBaseColorFactor(baseColor);
                    cpuMat.baseColorFactor = baseColor;
                    matEditor.dirty = true;
                }

                glm::vec3 emissive = glm::vec3(p.emissiveFactor);
                if (ImGui::ColorEdit3("Emissive Color", glm::value_ptr(emissive)))
                {
                    gpuMat->setEmissiveFactor(emissive);
                    cpuMat.emissiveFactor = emissive;
                    matEditor.dirty = true;
                }

                float metallic = p.metallicFactor;
                if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f))
                {
                    gpuMat->setMetallic(metallic);
                    cpuMat.metallicFactor = metallic;
                    matEditor.dirty = true;
                }

                float roughness = p.roughnessFactor;
                if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f))
                {
                    gpuMat->setRoughness(roughness);
                    cpuMat.roughnessFactor = roughness;
                    matEditor.dirty = true;
                }

                float aoStrength = p.aoStrength;
                if (ImGui::SliderFloat("AO Strength", &aoStrength, 0.0f, 1.0f))
                {
                    gpuMat->setAoStrength(aoStrength);
                    cpuMat.aoStrength = aoStrength;
                    matEditor.dirty = true;
                }

                float normalScale = p.normalScale;
                if (ImGui::SliderFloat("Normal Strength", &normalScale, 0.0f, 4.0f))
                {
                    gpuMat->setNormalScale(normalScale);
                    cpuMat.normalScale = normalScale;
                    matEditor.dirty = true;
                }
            }

            if (ImGui::CollapsingHeader("UV"))
            {
                auto p = gpuMat->params();

                glm::vec2 uvScale = {p.uvTransform.x, p.uvTransform.y};
                glm::vec2 uvOffset = {p.uvTransform.z, p.uvTransform.w};

                if (ImGui::DragFloat2("UV Scale", glm::value_ptr(uvScale), 0.01f, -100.0f, 100.0f))
                {
                    gpuMat->setUVScale(uvScale);
                    cpuMat.uvScale = uvScale;
                    matEditor.dirty = true;
                }

                if (ImGui::DragFloat2("UV Offset", glm::value_ptr(uvOffset), 0.01f, -100.0f, 100.0f))
                {
                    gpuMat->setUVOffset(uvOffset);
                    cpuMat.uvOffset = uvOffset;
                    matEditor.dirty = true;
                }
            }

            if (ImGui::CollapsingHeader("Advanced"))
            {
                auto p = gpuMat->params();
                uint32_t flags = p.flags;

                bool alphaMask = (flags & engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK) != 0;
                bool alphaBlend = (flags & engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND) != 0;

                if (ImGui::Checkbox("Masked", &alphaMask))
                {
                    if (alphaMask)
                        alphaBlend = false;
                    uint32_t newFlags = 0;
                    if (alphaMask)
                        newFlags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK;
                    if (alphaBlend)
                        newFlags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND;

                    gpuMat->setFlags(newFlags);
                    cpuMat.flags = newFlags;
                    matEditor.dirty = true;
                }

                if (ImGui::Checkbox("Translucent", &alphaBlend))
                {
                    if (alphaBlend)
                        alphaMask = false;
                    uint32_t newFlags = 0;
                    if (alphaMask)
                        newFlags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK;
                    if (alphaBlend)
                        newFlags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND;

                    gpuMat->setFlags(newFlags);
                    cpuMat.flags = newFlags;
                    matEditor.dirty = true;
                }

                float alphaCutoff = p.alphaCutoff;
                if (ImGui::SliderFloat("Alpha Cutoff", &alphaCutoff, 0.0f, 1.0f))
                {
                    gpuMat->setAlphaCutoff(alphaCutoff);
                    cpuMat.alphaCutoff = alphaCutoff;
                    matEditor.dirty = true;
                }
            }

            if (ImGui::CollapsingHeader("Textures"))
            {
                struct TextureSlotRow
                {
                    const char *label;
                    TextureUsage usage;
                    std::string *cpuPath;
                    std::function<void(const std::string &)> assignToGpu;
                };

                TextureSlotRow rows[] =
                    {
                        {"Albedo", TextureUsage::Color, &cpuMat.albedoTexture, [&](const std::string &p)
                         {
                             if (p.empty())
                                 gpuMat->setAlbedoTexture(nullptr);
                             else
                                 gpuMat->setAlbedoTexture(ensureProjectTextureLoaded(p, TextureUsage::Color));
                         }},
                        {"Normal", TextureUsage::Data, &cpuMat.normalTexture, [&](const std::string &p)
                         {
                             if (p.empty())
                                 gpuMat->setNormalTexture(nullptr);
                             else
                                 gpuMat->setNormalTexture(ensureProjectTextureLoaded(p, TextureUsage::Data));
                         }},
                        {"ORM", TextureUsage::Data, &cpuMat.ormTexture, [&](const std::string &p)
                         {
                             if (p.empty())
                                 gpuMat->setOrmTexture(nullptr);
                             else
                                 gpuMat->setOrmTexture(ensureProjectTextureLoaded(p, TextureUsage::Data));
                         }},
                        {"Emissive", TextureUsage::Color, &cpuMat.emissiveTexture, [&](const std::string &p)
                         {
                             if (p.empty())
                                 gpuMat->setEmissiveTexture(nullptr);
                             else
                                 gpuMat->setEmissiveTexture(ensureProjectTextureLoaded(p, TextureUsage::Color));
                         }},
                    };

                for (auto &row : rows)
                {
                    ImGui::PushID(row.label);

                    ImGui::BeginGroup();

                    auto slotTexture = ensureProjectTextureLoaded(*row.cpuPath, row.usage);
                    auto ds = m_assetsPreviewSystem.getOrRequestTexturePreview(*row.cpuPath, slotTexture);
                    ImTextureID texId = (ImTextureID)(uintptr_t)ds;

                    if (ImGui::ImageButton("##thumb", texId, ImVec2(56, 56)))
                    {
                        ui.openTexturePopup = true;
                        ui.texturePopupSlot = row.label;
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                        {
                            std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
                            const std::string normalizedDroppedPath = resolveTexturePathAgainstProjectRoot(droppedPath, projectRoot);

                            if (isTextureAssetPath(std::filesystem::path(normalizedDroppedPath)))
                            {
                                const std::string textureReferencePath = toMaterialTextureReferencePath(normalizedDroppedPath, projectRoot);
                                *row.cpuPath = textureReferencePath;
                                row.assignToGpu(textureReferencePath);
                                matEditor.dirty = true;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::EndGroup();

                    ImGui::SameLine();

                    ImGui::BeginGroup();
                    ImGui::TextUnformatted(row.label);

                    const std::string displayedTextureName = makeTextureAssetDisplayName(*row.cpuPath);
                    ImGui::TextWrapped("%s", displayedTextureName.c_str());

                    if (ImGui::Button("Use Default"))
                    {
                        row.cpuPath->clear();
                        row.assignToGpu("");
                        matEditor.dirty = true;
                    }

                    ImGui::EndGroup();

                    if (ui.openTexturePopup && ui.texturePopupSlot == row.label)
                    {
                        std::string popupName = std::string("TexturePicker##") + matPath;
                        ImGui::OpenPopup(popupName.c_str());
                        ui.openTexturePopup = false;
                    }

                    std::string popupName = std::string("TexturePicker##") + matPath;
                    if (ImGui::BeginPopup(popupName.c_str()))
                    {
                        ImGui::Text("Select Texture for %s", row.label);
                        ImGui::Separator();

                        ImGui::InputTextWithHint("##Search", "Search textures...", ui.textureFilter, sizeof(ui.textureFilter));
                        ImGui::SameLine();
                        if (ImGui::Button("X"))
                            ui.textureFilter[0] = '\0';

                        ImGui::Separator();

                        if (ImGui::Selectable("<Default>"))
                        {
                            row.cpuPath->clear();
                            row.assignToGpu("");
                            matEditor.dirty = true;
                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::BeginChild("TextureScroll", ImVec2(360, 260), true);
                        const std::string currentTextureReference = toMaterialTextureReferencePath(*row.cpuPath, projectRoot);
                        const auto textureAssetPaths = gatherProjectTextureAssets(*project, projectRoot);

                        for (const auto &texturePath : textureAssetPaths)
                        {
                            const std::string normalizedTexturePath = resolveTexturePathAgainstProjectRoot(texturePath, projectRoot);
                            const std::string textureReferencePath = toMaterialTextureReferencePath(normalizedTexturePath, projectRoot);
                            const std::string textureDisplayName = makeTextureAssetDisplayName(textureReferencePath);

                            if (ui.textureFilter[0] != '\0')
                            {
                                std::string pathLower = toLowerCopy(textureReferencePath);
                                std::string filterLower = ui.textureFilter;
                                std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

                                if (pathLower.find(filterLower) == std::string::npos &&
                                    toLowerCopy(textureDisplayName).find(filterLower) == std::string::npos)
                                    continue;
                            }

                            ImGui::PushID(textureReferencePath.c_str());

                            auto slotTexture = ensureProjectTextureLoaded(normalizedTexturePath, row.usage);
                            auto texDS = m_assetsPreviewSystem.getOrRequestTexturePreview(normalizedTexturePath, slotTexture);
                            ImTextureID imguiTexId = (ImTextureID)(uintptr_t)texDS;

                            bool selected = currentTextureReference == textureReferencePath;

                            if (selected)
                                ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 200, 255, 255));

                            if (ImGui::ImageButton("##pick", imguiTexId, ImVec2(48, 48)))
                            {
                                *row.cpuPath = textureReferencePath;
                                row.assignToGpu(textureReferencePath);
                                matEditor.dirty = true;
                                ImGui::CloseCurrentPopup();
                            }

                            if (selected)
                                ImGui::PopStyleColor();

                            if (ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                ImGui::Text("%s", textureReferencePath.c_str());
                                ImGui::EndTooltip();
                            }

                            ImGui::SameLine();

                            ImGui::BeginGroup();
                            ImGui::TextWrapped("%s", textureDisplayName.c_str());
                            ImGui::EndGroup();

                            ImGui::Separator();
                            ImGui::PopID();
                        }

                        ImGui::EndChild();

                        if (ImGui::Button("Close"))
                            ImGui::CloseCurrentPopup();

                        ImGui::EndPopup();
                    }

                    ImGui::Separator();
                    ImGui::PopID();
                }
            }

            ImGui::Spacing();
            if (ImGui::Button("Open Material Graph"))
            {
                // TODO: open graph editor asset window (separate system)
                // openMaterialGraphEditor(matPath);
            }
        }

        ImGui::End();

        matEditor.open = keepOpen;
        if (!matEditor.open)
            it = m_openMaterialEditors.erase(it);
        else
            ++it;
    }
}

void Editor::drawDocument()
{
    if (!m_showDocumentWindow || m_openDocumentPath.empty())
        return;

    if (!std::filesystem::exists(m_openDocumentPath) || std::filesystem::is_directory(m_openDocumentPath))
    {
        VX_EDITOR_WARNING_STREAM("Document source no longer exists: " << m_openDocumentPath);
        m_notificationManager.showWarning("Opened document was removed");
        m_openDocumentPath.clear();
        m_openDocumentSavedText.clear();
        m_showDocumentWindow = false;
        return;
    }

    std::string windowName = m_openDocumentPath.filename().string() + "###Document";

    if (m_centerDockId != 0)
        ImGui::SetNextWindowDockID(m_centerDockId, ImGuiCond_FirstUseEver);

    bool keepOpen = m_showDocumentWindow;
    if (!ImGui::Begin(windowName.c_str(), &keepOpen))
    {
        ImGui::End();
        m_showDocumentWindow = keepOpen;
        return;
    }

    const bool isShaderDocument = engine::shaders::ShaderCompiler::isCompilableShaderSource(m_openDocumentPath);
    const bool isDocumentFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool isDirty = !m_openDocumentPath.empty() && (m_textEditor.GetText() != m_openDocumentSavedText);

    if (ImGui::Button("Save"))
    {
        saveOpenDocument();
    }

    if (isShaderDocument)
    {
        ImGui::SameLine();
        if (ImGui::Button("Compile Shader"))
            compileOpenDocumentShader();
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(isDirty ? "*" : "");

    ImGui::SameLine();
    ImGui::TextDisabled("Lang: %s", m_textEditor.GetLanguageDefinition().mName.c_str());

    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_openDocumentPath.string().c_str());

    m_textEditor.Render("TextEditor");

    ImGuiIO &io = ImGui::GetIO();
    const bool isCtrlDown = io.KeyCtrl;
    const bool isShiftDown = io.KeyShift;

    if (isDocumentFocused && isCtrlDown && ImGui::IsKeyPressed(ImGuiKey_S, false))
        saveOpenDocument();

    if (isShaderDocument && isCtrlDown && isShiftDown && ImGui::IsKeyPressed(ImGuiKey_B, false))
        compileOpenDocumentShader();

    ImGui::End();
    m_showDocumentWindow = keepOpen;

    if (!m_showDocumentWindow)
    {
        m_openDocumentPath.clear();
        m_openDocumentSavedText.clear();
        m_textEditor.SetText("");
    }
}

void Editor::openTextDocument(const std::filesystem::path &path)
{
    if (path.empty() || !std::filesystem::exists(path) || std::filesystem::is_directory(path) || !isEditableTextPath(path))
    {
        VX_EDITOR_WARNING_STREAM("Open document failed. Invalid path: " << path);
        return;
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        VX_EDITOR_ERROR_STREAM("Failed to open document: " << path);
        m_notificationManager.showError("Failed to open file");
        return;
    }

    std::stringstream stream;
    stream << file.rdbuf();
    file.close();

    m_openDocumentPath = path;
    m_openDocumentSavedText = stream.str();
    m_textEditor.SetText(m_openDocumentSavedText);
    setDocumentLanguageFromPath(path);
    m_showDocumentWindow = true;

    VX_EDITOR_INFO_STREAM("Opened document: " << path);
    m_notificationManager.showInfo("Opened: " + path.filename().string());
}

bool Editor::saveOpenDocument()
{
    if (m_openDocumentPath.empty())
        return false;

    std::ofstream file(m_openDocumentPath, std::ios::trunc);

    if (!file.is_open())
    {
        VX_EDITOR_ERROR_STREAM("Failed to save document: " << m_openDocumentPath);
        m_notificationManager.showError("Failed to save file");
        return false;
    }

    const std::string text = m_textEditor.GetText();
    file << text;
    file.close();

    if (!file.good())
    {
        VX_EDITOR_ERROR_STREAM("Failed to write document: " << m_openDocumentPath);
        m_notificationManager.showError("Failed to write file");
        return false;
    }

    m_openDocumentSavedText = text;
    VX_EDITOR_INFO_STREAM("Saved document: " << m_openDocumentPath);
    m_notificationManager.showSuccess("File saved");

    return true;
}

bool Editor::compileOpenDocumentShader()
{
    if (m_openDocumentPath.empty())
        return false;

    if (!engine::shaders::ShaderCompiler::isCompilableShaderSource(m_openDocumentPath))
    {
        m_notificationManager.showWarning("This file is not a compilable shader");
        return false;
    }

    if (m_textEditor.GetText() != m_openDocumentSavedText)
    {
        if (!saveOpenDocument())
            return false;
    }

    std::string compileError;
    if (!engine::shaders::ShaderCompiler::compileFileToSpv(m_openDocumentPath, &compileError))
    {
        VX_EDITOR_ERROR_STREAM(compileError);
        m_notificationManager.showError("Shader compile failed");
        return false;
    }

    m_pendingShaderReloadRequest = true;
    VX_EDITOR_INFO_STREAM("Compiled shader: " << m_openDocumentPath << " -> " << (m_openDocumentPath.string() + ".spv"));
    m_notificationManager.showSuccess("Shader compiled and reload requested");
    return true;
}

void Editor::setDocumentLanguageFromPath(const std::filesystem::path &path)
{
    const std::string extension = toLowerCopy(path.extension().string());

    if (engine::shaders::ShaderCompiler::isCompilableShaderSource(path))
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::GLSL());
        return;
    }

    if (extension == ".hlsl" || extension == ".fx")
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::HLSL());
        return;
    }

    if (extension == ".cpp" || extension == ".cxx" || extension == ".cc" || extension == ".c" ||
        extension == ".hpp" || extension == ".hh" || extension == ".hxx" || extension == ".h")
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
        return;
    }

    if (extension == ".json" || extension == ".scene")
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(jsonLanguageDefinition());
        return;
    }

    if (extension == ".ini" || extension == ".cfg" || extension == ".toml" ||
        extension == ".yaml" || extension == ".yml" || extension == ".elixproject")
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(iniLanguageDefinition());
        return;
    }

    if (extension == ".sql")
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::SQL());
        return;
    }

    if (extension == ".lua")
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
        return;
    }

    if (extension == ".cmake")
    {
        m_textEditor.SetColorizerEnable(true);
        m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::C());
        return;
    }

    m_textEditor.SetColorizerEnable(false);
    m_textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::C());
}

void Editor::setSelectedEntity(engine::Entity *entity)
{
    if (m_selectedEntity != entity)
    {
        m_selectedMeshSlot.reset();
        m_isColliderHandleActive = false;
        m_isColliderHandleHovered = false;
        m_activeColliderHandle = ColliderHandleType::NONE;
    }

    m_selectedEntity = entity;

    if (m_selectedEntity)
        m_detailsContext = DetailsContext::Entity;

    if (m_selectedEntity)
        VX_EDITOR_DEBUG_STREAM("Selected entity: " << m_selectedEntity->getName() << " (id: " << m_selectedEntity->getId() << ")");
    else
        VX_EDITOR_DEBUG_STREAM("Selection cleared");
}

void Editor::focusSelectedEntity()
{
    if (!m_selectedEntity || !m_editorCamera)
        return;

    auto transformComponent = m_selectedEntity->getComponent<engine::Transform3DComponent>();
    const glm::mat4 model = transformComponent ? transformComponent->getMatrix() : glm::mat4(1.0f);

    glm::vec3 localMin{0.0f};
    glm::vec3 localMax{0.0f};
    bool hasBounds = false;

    if (auto staticMeshComponent = m_selectedEntity->getComponent<engine::StaticMeshComponent>())
        hasBounds = computeLocalBoundsFromMeshes(staticMeshComponent->getMeshes(), localMin, localMax);
    else if (auto skeletalMeshComponent = m_selectedEntity->getComponent<engine::SkeletalMeshComponent>())
        hasBounds = computeLocalBoundsFromMeshes(skeletalMeshComponent->getMeshes(), localMin, localMax);

    glm::vec3 localCenter{0.0f};
    float localRadius = 0.5f;

    if (hasBounds)
    {
        localCenter = (localMin + localMax) * 0.5f;
        localRadius = std::max(0.5f * glm::length(localMax - localMin), 0.1f);
    }

    const glm::vec3 worldCenter = glm::vec3(model * glm::vec4(localCenter, 1.0f));
    const float maxScale = std::max(
        {glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1])), glm::length(glm::vec3(model[2])), 0.001f});
    const float worldRadius = std::max(localRadius * maxScale, 0.25f);

    const float halfFovRadians = glm::radians(std::max(m_editorCamera->getFOV() * 0.5f, 1.0f));
    const float focusDistance = std::max((worldRadius / std::tan(halfFovRadians)) * 1.35f, 1.0f);

    glm::vec3 forward = m_editorCamera->getForward();
    if (glm::length(forward) <= 0.0001f)
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    else
        forward = glm::normalize(forward);

    const glm::vec3 newPosition = worldCenter - forward * focusDistance;
    m_editorCamera->setPosition(newPosition);

    glm::vec3 lookDirection = worldCenter - newPosition;
    if (glm::length(lookDirection) > 0.0001f)
    {
        lookDirection = glm::normalize(lookDirection);
        const float yaw = glm::degrees(std::atan2(lookDirection.z, lookDirection.x));
        const float pitch = glm::degrees(std::asin(glm::clamp(lookDirection.y, -1.0f, 1.0f)));

        m_editorCamera->setYaw(yaw);
        m_editorCamera->setPitch(pitch);
    }
}

void Editor::handleInput()
{
    ImGuiIO &io = ImGui::GetIO();

    if (!io.WantCaptureKeyboard)
    {
    }

    const bool isCtrlDown = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);

    if (isCtrlDown && ImGui::IsKeyPressed(ImGuiKey_C, false))
    {
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && m_selectedEntity)
    {
        m_scene->destroyEntity(m_selectedEntity);
        setSelectedEntity(nullptr);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        setSelectedEntity(nullptr);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && m_currentMode != EditorMode::EDIT)
        changeMode(EditorMode::EDIT);

    if (ImGui::IsKeyPressed(ImGuiKey_W))
    {
        m_currentGuizmoOperation = GuizmoOperation::TRANSLATE;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_E))
    {
        m_currentGuizmoOperation = GuizmoOperation::ROTATE;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_R))
    {
        m_currentGuizmoOperation = GuizmoOperation::SCALE;
    }
}

void Editor::openMaterialEditor(const std::filesystem::path &path)
{
    for (auto &mat : m_openMaterialEditors)
    {
        if (mat.path == path)
        {
            mat.open = true;
            return;
        }
    }

    OpenMaterialEditor editor;
    editor.path = path;
    editor.open = true;
    editor.dirty = false;
    m_openMaterialEditors.push_back(std::move(editor));
}

engine::Texture::SharedPtr Editor::ensureProjectTextureLoaded(const std::string &texturePath, TextureUsage usage)
{
    if (texturePath.empty())
        return nullptr;

    auto project = m_currentProject.lock();
    if (!project)
        return nullptr;

    const std::filesystem::path projectRoot = std::filesystem::path(project->fullPath);
    const std::string normalizedTexturePath = resolveTexturePathAgainstProjectRoot(texturePath, projectRoot);

    auto &record = project->cache.texturesByPath[normalizedTexturePath];
    record.path = normalizedTexturePath;

    if (auto cached = record.getGpuVariant(usage))
        return cached;

    auto texture = engine::AssetsLoader::loadTextureGPU(normalizedTexturePath, getLdrTextureFormat(usage));
    if (!texture)
    {
        VX_EDITOR_ERROR_STREAM("Failed to load texture for material: " << normalizedTexturePath << '\n');
        return nullptr;
    }

    record.setGpuVariant(usage, texture);
    record.loaded = true;

    return texture;
}

bool Editor::saveMaterialToDisk(const std::filesystem::path &path, const engine::CPUMaterial &cpuMaterial)
{
    nlohmann::json json;
    std::filesystem::path projectRoot;
    if (auto project = m_currentProject.lock(); project)
        projectRoot = std::filesystem::path(project->fullPath);

    const std::string albedoTexturePath = toMaterialTextureReferencePath(cpuMaterial.albedoTexture, projectRoot);
    const std::string normalTexturePath = toMaterialTextureReferencePath(cpuMaterial.normalTexture, projectRoot);
    const std::string ormTexturePath = toMaterialTextureReferencePath(cpuMaterial.ormTexture, projectRoot);
    const std::string emissiveTexturePath = toMaterialTextureReferencePath(cpuMaterial.emissiveTexture, projectRoot);

    json["name"] = cpuMaterial.name.empty() ? path.stem().string() : cpuMaterial.name;
    json["texture_path"] = albedoTexturePath;
    json["normal_texture"] = normalTexturePath;
    json["orm_texture"] = ormTexturePath;
    json["emissive_texture"] = emissiveTexturePath;
    json["color"] = {cpuMaterial.baseColorFactor.r, cpuMaterial.baseColorFactor.g, cpuMaterial.baseColorFactor.b, cpuMaterial.baseColorFactor.a};
    json["emissive"] = {cpuMaterial.emissiveFactor.r, cpuMaterial.emissiveFactor.g, cpuMaterial.emissiveFactor.b};
    json["metallic"] = cpuMaterial.metallicFactor;
    json["roughness"] = cpuMaterial.roughnessFactor;
    json["ao_strength"] = cpuMaterial.aoStrength;
    json["normal_scale"] = cpuMaterial.normalScale;
    json["alpha_cutoff"] = cpuMaterial.alphaCutoff;
    json["flags"] = cpuMaterial.flags;
    json["uv_scale"] = {cpuMaterial.uvScale.x, cpuMaterial.uvScale.y};
    json["uv_offset"] = {cpuMaterial.uvOffset.x, cpuMaterial.uvOffset.y};

    std::ofstream file(path);
    if (!file.is_open())
    {
        VX_EDITOR_ERROR_STREAM("Failed to open material for writing: " << path << '\n');
        return false;
    }

    file << std::setw(4) << json << '\n';

    if (!file.good())
    {
        VX_EDITOR_ERROR_STREAM("Failed to write material file: " << path << '\n');
        return false;
    }

    return true;
}

bool Editor::reloadMaterialFromDisk(const std::filesystem::path &path)
{
    auto project = m_currentProject.lock();
    if (!project)
        return false;

    auto materialAsset = engine::AssetsLoader::loadMaterial(path.string());
    if (!materialAsset.has_value())
    {
        VX_EDITOR_ERROR_STREAM("Failed to reload material asset from disk: " << path << '\n');
        return false;
    }

    auto cpuMaterial = materialAsset.value().material;
    if (cpuMaterial.name.empty())
        cpuMaterial.name = path.stem().string();

    const std::filesystem::path projectRoot = std::filesystem::path(project->fullPath);
    normalizeMaterialTexturePaths(cpuMaterial, path, projectRoot);

    auto &record = project->cache.materialsByPath[path.string()];
    record.path = path.string();
    record.cpuData = cpuMaterial;

    if (!record.gpu)
        record.gpu = engine::Material::create(ensureProjectTextureLoaded(cpuMaterial.albedoTexture, TextureUsage::Color));

    if (!record.gpu)
        return false;

    record.gpu->setAlbedoTexture(ensureProjectTextureLoaded(cpuMaterial.albedoTexture, TextureUsage::Color));
    record.gpu->setNormalTexture(ensureProjectTextureLoaded(cpuMaterial.normalTexture, TextureUsage::Data));
    record.gpu->setOrmTexture(ensureProjectTextureLoaded(cpuMaterial.ormTexture, TextureUsage::Data));
    record.gpu->setEmissiveTexture(ensureProjectTextureLoaded(cpuMaterial.emissiveTexture, TextureUsage::Color));
    record.gpu->setBaseColorFactor(cpuMaterial.baseColorFactor);
    record.gpu->setEmissiveFactor(cpuMaterial.emissiveFactor);
    record.gpu->setMetallic(cpuMaterial.metallicFactor);
    record.gpu->setRoughness(cpuMaterial.roughnessFactor);
    record.gpu->setAoStrength(cpuMaterial.aoStrength);
    record.gpu->setNormalScale(cpuMaterial.normalScale);
    record.gpu->setAlphaCutoff(cpuMaterial.alphaCutoff);
    record.gpu->setFlags(cpuMaterial.flags);
    record.gpu->setUVScale(cpuMaterial.uvScale);
    record.gpu->setUVOffset(cpuMaterial.uvOffset);

    record.texture = ensureProjectTextureLoaded(cpuMaterial.albedoTexture, TextureUsage::Color);

    return true;
}

engine::Material::SharedPtr Editor::ensureMaterialLoaded(const std::string &materialPath)
{
    if (materialPath.empty())
        return nullptr;

    auto project = m_currentProject.lock();
    if (!project)
        return nullptr;

    auto it = project->cache.materialsByPath.find(materialPath);
    if (it != project->cache.materialsByPath.end() && it->second.gpu)
        return it->second.gpu;

    m_assetsPreviewSystem.getOrRequestMaterialPreview(materialPath);

    it = project->cache.materialsByPath.find(materialPath);
    if (it != project->cache.materialsByPath.end() && it->second.gpu)
        return it->second.gpu;

    return nullptr;
}

const engine::ModelAsset *Editor::ensureModelAssetLoaded(const std::string &modelPath)
{
    if (modelPath.empty())
        return nullptr;

    auto project = m_currentProject.lock();
    if (!project)
        return nullptr;

    auto it = project->cache.modelsByPath.find(modelPath);
    if (it != project->cache.modelsByPath.end() && it->second.cpuData.has_value())
        return &it->second.cpuData.value();

    auto modelAsset = engine::AssetsLoader::loadModel(modelPath);
    if (!modelAsset.has_value())
        return nullptr;

    auto &record = project->cache.modelsByPath[modelPath];
    record.path = modelPath;
    record.cpuData = modelAsset.value();
    return &record.cpuData.value();
}

void Editor::invalidateModelDetailsCache()
{
    m_modelDetailsCache = ModelDetailsCache{};
    m_modelDetailsCacheAssetPath.clear();
    m_modelDetailsCacheSearchDirectory.clear();
    m_modelDetailsCacheDirty = true;
}

void Editor::rebuildModelDetailsCache(const engine::ModelAsset &modelAsset,
                                      const std::filesystem::path &modelDirectory,
                                      const std::filesystem::path &projectRoot,
                                      const std::filesystem::path &textureSearchDirectory)
{
    m_modelDetailsCache = ModelDetailsCache{};

    m_modelDetailsCache.materials.reserve(modelAsset.meshes.size());
    std::unordered_map<std::string, size_t> materialIndexBySignature;
    materialIndexBySignature.reserve(modelAsset.meshes.size());

    std::unordered_set<std::string> unresolvedTexturePaths;
    unresolvedTexturePaths.reserve(modelAsset.meshes.size() * 2);

    auto resolveTextureForDisplay = [&](const std::string &rawTexturePath) -> std::string
    {
        bool resolved = true;
        return resolveTexturePathForMaterialExport(rawTexturePath,
                                                   modelDirectory,
                                                   projectRoot,
                                                   textureSearchDirectory,
                                                   m_modelTextureManualOverrides,
                                                   resolved);
    };

    auto collectUnresolvedPath = [&](const std::string &texturePath)
    {
        bool resolved = true;
        resolveTexturePathForMaterialExport(texturePath,
                                            modelDirectory,
                                            projectRoot,
                                            textureSearchDirectory,
                                            m_modelTextureManualOverrides,
                                            resolved);

        if (!resolved && !texturePath.empty())
            unresolvedTexturePaths.insert(texturePath);
    };

    for (size_t meshIndex = 0; meshIndex < modelAsset.meshes.size(); ++meshIndex)
    {
        const auto &mesh = modelAsset.meshes[meshIndex];
        m_modelDetailsCache.totalIndexCount += mesh.indices.size();

        if (mesh.vertexStride > 0)
            m_modelDetailsCache.totalVertexCount += mesh.vertexData.size() / mesh.vertexStride;

        auto material = mesh.material;
        if (material.name.empty())
            material.name = mesh.name.empty() ? ("Material_" + std::to_string(meshIndex)) : mesh.name;

        collectUnresolvedPath(material.albedoTexture);
        collectUnresolvedPath(material.normalTexture);
        collectUnresolvedPath(material.ormTexture);
        collectUnresolvedPath(material.emissiveTexture);

        const std::string signature = buildMaterialSignature(material);
        const auto [iterator, inserted] = materialIndexBySignature.emplace(signature, m_modelDetailsCache.materials.size());

        if (inserted)
        {
            ModelMaterialOverviewEntry overviewEntry{};
            overviewEntry.material = material;
            overviewEntry.albedoDisplayPath = material.albedoTexture.empty() ? std::string("-") : resolveTextureForDisplay(material.albedoTexture);
            overviewEntry.normalDisplayPath = material.normalTexture.empty() ? std::string("-") : resolveTextureForDisplay(material.normalTexture);
            overviewEntry.ormDisplayPath = material.ormTexture.empty() ? std::string("-") : resolveTextureForDisplay(material.ormTexture);
            overviewEntry.emissiveDisplayPath = material.emissiveTexture.empty() ? std::string("-") : resolveTextureForDisplay(material.emissiveTexture);
            m_modelDetailsCache.materials.push_back(std::move(overviewEntry));
        }

        m_modelDetailsCache.materials[iterator->second].meshUsageCount += 1;
    }

    m_modelDetailsCache.hasSkeleton = modelAsset.skeleton.has_value();
    m_modelDetailsCache.animationCount = modelAsset.animations.size();
    m_modelDetailsCache.unresolvedTexturePaths.assign(unresolvedTexturePaths.begin(), unresolvedTexturePaths.end());
    std::sort(m_modelDetailsCache.unresolvedTexturePaths.begin(), m_modelDetailsCache.unresolvedTexturePaths.end());
    m_modelDetailsCacheSearchDirectory = textureSearchDirectory;
    m_modelDetailsCacheDirty = false;
}

bool Editor::buildPerMeshMaterialPathsFromDirectory(const engine::ModelAsset &modelAsset,
                                                    const std::filesystem::path &materialsDirectory,
                                                    std::vector<std::string> &outPerMeshMaterialPaths,
                                                    size_t &outMatchedSlots) const
{
    outPerMeshMaterialPaths.assign(modelAsset.meshes.size(), {});
    outMatchedSlots = 0;

    std::error_code directoryError;
    if (materialsDirectory.empty() ||
        !std::filesystem::exists(materialsDirectory, directoryError) ||
        directoryError ||
        !std::filesystem::is_directory(materialsDirectory, directoryError) ||
        directoryError)
        return false;

    std::unordered_map<std::string, std::string> materialPathBySignature;
    std::unordered_map<std::string, std::string> materialPathByName;
    std::vector<std::filesystem::path> discoveredMaterialPaths;
    discoveredMaterialPaths.reserve(128);

    std::error_code iteratorError;
    for (std::filesystem::directory_iterator iterator(materialsDirectory, iteratorError);
         !iteratorError && iterator != std::filesystem::directory_iterator();
         iterator.increment(iteratorError))
    {
        const auto &entry = *iterator;
        std::error_code fileStatusError;
        if (!entry.is_regular_file(fileStatusError) || fileStatusError)
            continue;

        const std::filesystem::path materialPath = entry.path().lexically_normal();
        if (toLowerCopy(materialPath.extension().string()) != ".elixmat")
            continue;

        discoveredMaterialPaths.push_back(materialPath);

        auto materialAsset = engine::AssetsLoader::loadMaterial(materialPath.string());
        if (!materialAsset.has_value())
            continue;

        const auto &material = materialAsset.value().material;
        const std::string signature = buildMaterialSignature(material);

        if (!signature.empty() && materialPathBySignature.find(signature) == materialPathBySignature.end())
            materialPathBySignature.emplace(signature, materialPath.string());

        if (!material.name.empty() && materialPathByName.find(material.name) == materialPathByName.end())
            materialPathByName.emplace(material.name, materialPath.string());
    }

    if (iteratorError)
        return false;

    if (discoveredMaterialPaths.empty())
        return false;

    for (size_t meshIndex = 0; meshIndex < modelAsset.meshes.size(); ++meshIndex)
    {
        const auto &mesh = modelAsset.meshes[meshIndex];
        engine::CPUMaterial candidate = mesh.material;
        if (candidate.name.empty())
            candidate.name = mesh.name.empty() ? ("Material_" + std::to_string(meshIndex)) : mesh.name;

        std::string resolvedMaterialPath;

        const std::string signature = buildMaterialSignature(candidate);
        if (auto signatureIt = materialPathBySignature.find(signature); signatureIt != materialPathBySignature.end())
            resolvedMaterialPath = signatureIt->second;

        if (resolvedMaterialPath.empty())
        {
            if (auto nameIt = materialPathByName.find(candidate.name); nameIt != materialPathByName.end())
                resolvedMaterialPath = nameIt->second;
        }

        if (resolvedMaterialPath.empty())
        {
            const std::string expectedStem = sanitizeFileStem(candidate.name);
            for (const auto &materialPath : discoveredMaterialPaths)
            {
                const std::string stem = materialPath.stem().string();
                if (stem == expectedStem || stem.rfind(expectedStem + "_", 0) == 0)
                {
                    resolvedMaterialPath = materialPath.string();
                    break;
                }
            }
        }

        if (resolvedMaterialPath.empty())
            continue;

        outPerMeshMaterialPaths[meshIndex] = resolvedMaterialPath;
        ++outMatchedSlots;
    }

    return outMatchedSlots > 0;
}

bool Editor::applyPerMeshMaterialPathsToSelectedEntity(const std::vector<std::string> &perMeshMaterialPaths)
{
    if (!m_selectedEntity)
    {
        VX_EDITOR_WARNING_STREAM("Material auto-apply failed. No selected entity.");
        return false;
    }

    auto staticMeshComponent = m_selectedEntity->getComponent<engine::StaticMeshComponent>();
    auto skeletalMeshComponent = m_selectedEntity->getComponent<engine::SkeletalMeshComponent>();

    if (!staticMeshComponent && !skeletalMeshComponent)
    {
        VX_EDITOR_WARNING_STREAM("Material auto-apply failed for entity '" << m_selectedEntity->getName() << "'. Entity has no mesh component.");
        return false;
    }

    const size_t slotCount = staticMeshComponent ? staticMeshComponent->getMaterialSlotCount() : skeletalMeshComponent->getMaterialSlotCount();
    const size_t slotLimit = std::min(slotCount, perMeshMaterialPaths.size());

    if (slotLimit == 0)
        return false;

    size_t appliedCount = 0;
    size_t failedLoads = 0;

    for (size_t slot = 0; slot < slotLimit; ++slot)
    {
        const std::string &materialPath = perMeshMaterialPaths[slot];
        if (materialPath.empty())
            continue;

        auto material = ensureMaterialLoaded(materialPath);
        if (!material)
        {
            ++failedLoads;
            continue;
        }

        if (staticMeshComponent)
        {
            staticMeshComponent->setMaterialOverride(slot, material);
            staticMeshComponent->setMaterialOverridePath(slot, materialPath);
        }
        else
        {
            skeletalMeshComponent->setMaterialOverride(slot, material);
            skeletalMeshComponent->setMaterialOverridePath(slot, materialPath);
        }

        ++appliedCount;
    }

    if (appliedCount == 0)
    {
        VX_EDITOR_WARNING_STREAM("Material auto-apply failed for entity '" << m_selectedEntity->getName() << "'. No materials were assigned.");
        return false;
    }

    VX_EDITOR_INFO_STREAM("Auto-applied " << appliedCount << " material slot(s) to entity '" << m_selectedEntity->getName()
                                          << "' (requested " << perMeshMaterialPaths.size() << ", entity slots " << slotCount
                                          << ", failed loads " << failedLoads << ").");

    if (slotCount != perMeshMaterialPaths.size())
    {
        VX_EDITOR_WARNING_STREAM("Material auto-apply slot mismatch. Entity slots: " << slotCount
                                                                                     << ", exported slots: " << perMeshMaterialPaths.size());
    }

    return true;
}

bool Editor::exportModelMaterials(const std::filesystem::path &modelPath,
                                  const std::filesystem::path &outputDirectory,
                                  const std::filesystem::path &textureSearchDirectory,
                                  const std::unordered_map<std::string, std::string> &textureOverrides,
                                  std::vector<std::string> *outPerMeshMaterialPaths)
{
    auto project = m_currentProject.lock();
    if (!project)
        return false;

    const engine::ModelAsset *model = ensureModelAssetLoaded(modelPath.string());
    if (!model)
    {
        VX_EDITOR_ERROR_STREAM("Failed to export materials. Could not load model: " << modelPath);
        return false;
    }

    if (model->meshes.empty())
    {
        VX_EDITOR_WARNING_STREAM("Model has no meshes. Material export skipped for: " << modelPath);
        return false;
    }

    std::filesystem::path exportDirectory = outputDirectory;
    if (exportDirectory.empty())
        return false;

    if (exportDirectory.is_relative())
        exportDirectory = std::filesystem::path(project->fullPath) / exportDirectory;

    exportDirectory = makeAbsoluteNormalized(exportDirectory);

    std::error_code errorCode;
    std::filesystem::create_directories(exportDirectory, errorCode);
    if (errorCode)
    {
        VX_EDITOR_ERROR_STREAM("Failed to create material export directory '" << exportDirectory << "': " << errorCode.message());
        return false;
    }

    struct ExportMaterialEntry
    {
        engine::CPUMaterial material;
        size_t usageCount{0};
    };

    std::vector<ExportMaterialEntry> materialEntries;
    materialEntries.reserve(model->meshes.size());

    std::unordered_map<std::string, size_t> entryIndexBySignature;
    entryIndexBySignature.reserve(model->meshes.size());

    std::vector<std::string> materialSignatureByMeshSlot;
    materialSignatureByMeshSlot.reserve(model->meshes.size());

    for (size_t meshIndex = 0; meshIndex < model->meshes.size(); ++meshIndex)
    {
        const auto &mesh = model->meshes[meshIndex];
        engine::CPUMaterial candidate = mesh.material;

        if (candidate.name.empty())
            candidate.name = mesh.name.empty() ? ("Material_" + std::to_string(meshIndex)) : mesh.name;

        const std::string signature = buildMaterialSignature(candidate);
        materialSignatureByMeshSlot.push_back(signature);
        const auto [iterator, inserted] = entryIndexBySignature.emplace(signature, materialEntries.size());

        if (inserted)
            materialEntries.push_back({candidate, 0});

        materialEntries[iterator->second].usageCount += 1;
    }

    if (materialEntries.empty())
        return false;

    std::unordered_set<std::string> unresolvedTexturePaths;
    unresolvedTexturePaths.reserve(materialEntries.size() * 2);

    size_t exportedMaterials = 0;

    const std::filesystem::path modelDirectory = modelPath.parent_path();
    const std::filesystem::path projectRoot = std::filesystem::path(project->fullPath);
    std::unordered_map<std::string, std::string> exportedPathBySignature;
    exportedPathBySignature.reserve(materialEntries.size());

    for (size_t materialIndex = 0; materialIndex < materialEntries.size(); ++materialIndex)
    {
        auto material = materialEntries[materialIndex].material;

        auto resolveTextureField = [&](std::string &texturePath)
        {
            const std::string originalPath = texturePath;
            bool resolved = true;
            texturePath = toMaterialTextureReferencePath(resolveTexturePathForMaterialExport(texturePath,
                                                                                            modelDirectory,
                                                                                            projectRoot,
                                                                                            textureSearchDirectory,
                                                                                            textureOverrides,
                                                                                            resolved),
                                                         projectRoot);

            const std::filesystem::path resolvedTexturePath = std::filesystem::path(resolveTexturePathAgainstProjectRoot(texturePath, projectRoot)).lexically_normal();
            auto textureType = readSerializedAssetType(resolvedTexturePath);
            const bool isSerializedTexture = textureType.has_value() && textureType.value() == engine::Asset::AssetType::TEXTURE;

            if ((!resolved || !isSerializedTexture) && !originalPath.empty())
                unresolvedTexturePaths.insert(originalPath);
        };

        resolveTextureField(material.albedoTexture);
        resolveTextureField(material.normalTexture);
        resolveTextureField(material.ormTexture);
        resolveTextureField(material.emissiveTexture);

        if (material.name.empty())
            material.name = "Material_" + std::to_string(materialIndex);

        const std::string baseName = sanitizeFileStem(material.name);
        const std::filesystem::path materialPath = makeUniquePathWithExtension(exportDirectory, baseName, ".elixmat");

        if (!saveMaterialToDisk(materialPath, material))
        {
            VX_EDITOR_ERROR_STREAM("Failed to export material '" << material.name << "' to: " << materialPath);
            continue;
        }

        const std::string signature = buildMaterialSignature(materialEntries[materialIndex].material);
        exportedPathBySignature[signature] = materialPath.string();

        ++exportedMaterials;
    }

    if (exportedMaterials == 0)
    {
        VX_EDITOR_ERROR_STREAM("Material export failed for model: " << modelPath);
        return false;
    }

    VX_EDITOR_INFO_STREAM("Exported " << exportedMaterials << " material(s) from model '" << modelPath
                                      << "' to '" << exportDirectory << "'. Unresolved texture paths: " << unresolvedTexturePaths.size());

    if (!unresolvedTexturePaths.empty())
    {
        size_t logged = 0;
        for (const auto &path : unresolvedTexturePaths)
        {
            VX_EDITOR_WARNING_STREAM("Unresolved material texture path: " << path);
            if (++logged >= 24)
                break;
        }
    }

    m_notificationManager.showSuccess("Exported " + std::to_string(exportedMaterials) + " material(s)");
    if (!unresolvedTexturePaths.empty())
        m_notificationManager.showWarning(std::to_string(unresolvedTexturePaths.size()) + " texture path(s) unresolved");

    if (outPerMeshMaterialPaths)
    {
        outPerMeshMaterialPaths->assign(materialSignatureByMeshSlot.size(), {});

        for (size_t meshIndex = 0; meshIndex < materialSignatureByMeshSlot.size(); ++meshIndex)
        {
            auto exportedPathIt = exportedPathBySignature.find(materialSignatureByMeshSlot[meshIndex]);
            if (exportedPathIt == exportedPathBySignature.end())
                continue;

            (*outPerMeshMaterialPaths)[meshIndex] = exportedPathIt->second;
        }
    }

    return true;
}

void Editor::drawAssetDetails()
{
    if (m_selectedAssetPath.empty())
    {
        ImGui::TextUnformatted("Select an object or asset to view details");
        return;
    }

    if (!std::filesystem::exists(m_selectedAssetPath))
    {
        ImGui::TextDisabled("Selected asset no longer exists");
        if (ImGui::Button("Clear Asset Selection"))
        {
            m_selectedAssetPath.clear();
            invalidateModelDetailsCache();
        }
        return;
    }

    auto project = m_currentProject.lock();
    const std::filesystem::path projectRoot = project ? std::filesystem::path(project->fullPath) : std::filesystem::path{};
    const std::filesystem::path assetPath = m_selectedAssetPath;
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
            openMaterialEditor(assetPath);

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
            openTextDocument(assetPath);
        return;
    }

    if (!isModelAssetPath(assetPath))
        return;

    const engine::ModelAsset *modelAsset = ensureModelAssetLoaded(assetPath.string());
    ImGui::Separator();
    ImGui::TextUnformatted("Model Import");

    if (!modelAsset)
    {
        ImGui::TextDisabled("Failed to load model metadata");
        if (ImGui::Button("Retry"))
        {
            ensureModelAssetLoaded(assetPath.string());
            invalidateModelDetailsCache();
        }
        return;
    }

    const std::filesystem::path modelDirectory = assetPath.parent_path();

    if (m_lastModelDetailsAssetPath != assetPath)
    {
        m_lastModelDetailsAssetPath = assetPath;
        invalidateModelDetailsCache();

        const std::filesystem::path defaultExportPath = assetPath.parent_path() / (assetPath.stem().string() + "_Materials");
        std::memset(m_modelMaterialsExportDirectory, 0, sizeof(m_modelMaterialsExportDirectory));
        std::strncpy(m_modelMaterialsExportDirectory, defaultExportPath.string().c_str(), sizeof(m_modelMaterialsExportDirectory) - 1);

        std::memset(m_modelMaterialsTextureSearchDirectory, 0, sizeof(m_modelMaterialsTextureSearchDirectory));
        std::strncpy(m_modelMaterialsTextureSearchDirectory, assetPath.parent_path().string().c_str(), sizeof(m_modelMaterialsTextureSearchDirectory) - 1);

        m_modelTextureManualOverrides.clear();
        m_selectedUnresolvedTexturePath.clear();
        std::memset(m_selectedTextureOverrideBuffer, 0, sizeof(m_selectedTextureOverrideBuffer));
    }

    std::filesystem::path textureSearchDirectory = std::filesystem::path(m_modelMaterialsTextureSearchDirectory);
    if (!textureSearchDirectory.empty())
    {
        if (textureSearchDirectory.is_relative() && !projectRoot.empty())
            textureSearchDirectory = projectRoot / textureSearchDirectory;

        textureSearchDirectory = makeAbsoluteNormalized(textureSearchDirectory);
    }

    if (m_modelDetailsCacheDirty ||
        m_modelDetailsCacheAssetPath != assetPath ||
        m_modelDetailsCacheSearchDirectory != textureSearchDirectory)
    {
        rebuildModelDetailsCache(*modelAsset, modelDirectory, projectRoot, textureSearchDirectory);
        m_modelDetailsCacheAssetPath = assetPath;
    }

    auto &materials = m_modelDetailsCache.materials;
    auto &unresolvedTexturePathList = m_modelDetailsCache.unresolvedTexturePaths;

    if (!m_selectedUnresolvedTexturePath.empty() &&
        std::find(unresolvedTexturePathList.begin(), unresolvedTexturePathList.end(), m_selectedUnresolvedTexturePath) == unresolvedTexturePathList.end())
    {
        m_selectedUnresolvedTexturePath.clear();
        std::memset(m_selectedTextureOverrideBuffer, 0, sizeof(m_selectedTextureOverrideBuffer));
    }

    ImGui::Text("Meshes: %zu", modelAsset->meshes.size());
    ImGui::Text("Vertices: %zu", m_modelDetailsCache.totalVertexCount);
    ImGui::Text("Indices: %zu", m_modelDetailsCache.totalIndexCount);
    ImGui::Text("Unique materials: %zu", materials.size());
    ImGui::Text("Unresolved texture paths: %zu", unresolvedTexturePathList.size());
    ImGui::Text("Has skeleton: %s", m_modelDetailsCache.hasSkeleton ? "Yes" : "No");
    ImGui::Text("Animations: %zu", m_modelDetailsCache.animationCount);

    if (ImGui::CollapsingHeader("Texture Resolver", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextWrapped("Search folder remaps unresolved texture paths by filename (directory is replaced, filename stays unless you set manual override).");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("Search folder", m_modelMaterialsTextureSearchDirectory, sizeof(m_modelMaterialsTextureSearchDirectory)))
        {
            textureSearchDirectory = std::filesystem::path(m_modelMaterialsTextureSearchDirectory);
            if (!textureSearchDirectory.empty())
            {
                if (textureSearchDirectory.is_relative() && !projectRoot.empty())
                    textureSearchDirectory = projectRoot / textureSearchDirectory;

                textureSearchDirectory = makeAbsoluteNormalized(textureSearchDirectory);
            }

            m_modelDetailsCacheDirty = true;
        }

        if (ImGui::Button("Use Model Directory"))
        {
            std::memset(m_modelMaterialsTextureSearchDirectory, 0, sizeof(m_modelMaterialsTextureSearchDirectory));
            std::strncpy(m_modelMaterialsTextureSearchDirectory, assetPath.parent_path().string().c_str(), sizeof(m_modelMaterialsTextureSearchDirectory) - 1);
            textureSearchDirectory = makeAbsoluteNormalized(std::filesystem::path(m_modelMaterialsTextureSearchDirectory));
            m_modelDetailsCacheDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply Folder To All Unresolved"))
        {
            if (textureSearchDirectory.empty() || !std::filesystem::exists(textureSearchDirectory) || !std::filesystem::is_directory(textureSearchDirectory))
            {
                m_notificationManager.showError("Search folder is invalid");
            }
            else if (unresolvedTexturePathList.empty())
            {
                m_notificationManager.showInfo("No unresolved texture paths");
            }
            else
            {
                size_t mappedCount = 0;
                size_t missingCount = 0;
                bool overridesChanged = false;

                for (const auto &unresolvedPath : unresolvedTexturePathList)
                {
                    const std::string fileName = extractFileNamePortable(unresolvedPath);

                    if (fileName.empty())
                    {
                        ++missingCount;
                        continue;
                    }

                    std::filesystem::path remappedPath = textureSearchDirectory / fileName;
                    if (auto caseInsensitiveMatch = findCaseInsensitiveFileInDirectory(textureSearchDirectory, fileName);
                        caseInsensitiveMatch.has_value())
                        remappedPath = caseInsensitiveMatch.value();

                    remappedPath = remappedPath.lexically_normal();
                    const std::string remappedPathString = remappedPath.string();
                    auto overrideIt = m_modelTextureManualOverrides.find(unresolvedPath);
                    if (overrideIt == m_modelTextureManualOverrides.end() || overrideIt->second != remappedPathString)
                    {
                        m_modelTextureManualOverrides[unresolvedPath] = remappedPathString;
                        overridesChanged = true;
                    }

                    if (std::filesystem::exists(remappedPath))
                        ++mappedCount;
                    else
                        ++missingCount;
                }

                if (overridesChanged)
                    m_modelDetailsCacheDirty = true;

                if (mappedCount > 0)
                    m_notificationManager.showSuccess("Mapped " + std::to_string(mappedCount) + " texture path(s)");

                if (missingCount > 0)
                    m_notificationManager.showWarning(std::to_string(missingCount) + " path(s) still missing after remap");
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
                    const bool selected = texturePath == m_selectedUnresolvedTexturePath;
                    if (ImGui::Selectable(texturePath.c_str(), selected))
                    {
                        m_selectedUnresolvedTexturePath = texturePath;
                        std::memset(m_selectedTextureOverrideBuffer, 0, sizeof(m_selectedTextureOverrideBuffer));

                        auto overrideIt = m_modelTextureManualOverrides.find(texturePath);
                        const std::string &overrideValue = overrideIt != m_modelTextureManualOverrides.end() ? overrideIt->second : "";
                        std::strncpy(m_selectedTextureOverrideBuffer, overrideValue.c_str(), sizeof(m_selectedTextureOverrideBuffer) - 1);
                    }
                }

                ImGui::EndListBox();
            }

            if (!m_selectedUnresolvedTexturePath.empty())
            {
                ImGui::TextWrapped("Selected: %s", m_selectedUnresolvedTexturePath.c_str());
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("Override path", m_selectedTextureOverrideBuffer, sizeof(m_selectedTextureOverrideBuffer));

                if (ImGui::Button("Apply Override"))
                {
                    const std::string overridePath = m_selectedTextureOverrideBuffer;
                    auto overrideIt = m_modelTextureManualOverrides.find(m_selectedUnresolvedTexturePath);
                    if (overrideIt == m_modelTextureManualOverrides.end() || overrideIt->second != overridePath)
                    {
                        m_modelTextureManualOverrides[m_selectedUnresolvedTexturePath] = overridePath;
                        m_modelDetailsCacheDirty = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Override"))
                {
                    const size_t erasedCount = m_modelTextureManualOverrides.erase(m_selectedUnresolvedTexturePath);
                    std::memset(m_selectedTextureOverrideBuffer, 0, sizeof(m_selectedTextureOverrideBuffer));
                    if (erasedCount > 0)
                        m_modelDetailsCacheDirty = true;
                }

                bool previewResolved = true;
                const std::string previewPath = resolveTexturePathForMaterialExport(m_selectedUnresolvedTexturePath,
                                                                                    modelDirectory,
                                                                                    projectRoot,
                                                                                    textureSearchDirectory,
                                                                                    m_modelTextureManualOverrides,
                                                                                    previewResolved);
                ImGui::TextWrapped("Preview: %s", previewPath.empty() ? "<None>" : previewPath.c_str());
                ImGui::TextDisabled("%s", previewResolved ? "Status: Resolved" : "Status: Unresolved");
            }
        }

        if (!m_modelTextureManualOverrides.empty() && ImGui::TreeNode("Manual overrides"))
        {
            for (auto overrideIterator = m_modelTextureManualOverrides.begin(); overrideIterator != m_modelTextureManualOverrides.end();)
            {
                const std::string originalPath = overrideIterator->first;
                const std::string overridePath = overrideIterator->second;

                ImGui::PushID(originalPath.c_str());
                ImGui::TextWrapped("%s", originalPath.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    if (m_selectedUnresolvedTexturePath == originalPath)
                    {
                        m_selectedUnresolvedTexturePath.clear();
                        std::memset(m_selectedTextureOverrideBuffer, 0, sizeof(m_selectedTextureOverrideBuffer));
                    }

                    overrideIterator = m_modelTextureManualOverrides.erase(overrideIterator);
                    m_modelDetailsCacheDirty = true;
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
    ImGui::InputText("Output folder", m_modelMaterialsExportDirectory, sizeof(m_modelMaterialsExportDirectory));

    if (ImGui::Button("Use Model Folder"))
    {
        std::memset(m_modelMaterialsExportDirectory, 0, sizeof(m_modelMaterialsExportDirectory));
        std::strncpy(m_modelMaterialsExportDirectory, assetPath.parent_path().string().c_str(), sizeof(m_modelMaterialsExportDirectory) - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Use Default Export Folder"))
    {
        const std::filesystem::path defaultExportPath = assetPath.parent_path() / (assetPath.stem().string() + "_Materials");
        std::memset(m_modelMaterialsExportDirectory, 0, sizeof(m_modelMaterialsExportDirectory));
        std::strncpy(m_modelMaterialsExportDirectory, defaultExportPath.string().c_str(), sizeof(m_modelMaterialsExportDirectory) - 1);
    }

    const auto *selectedStaticMeshComponent = m_selectedEntity ? m_selectedEntity->getComponent<engine::StaticMeshComponent>() : nullptr;
    const auto *selectedSkeletalMeshComponent = m_selectedEntity ? m_selectedEntity->getComponent<engine::SkeletalMeshComponent>() : nullptr;
    const bool selectedEntityCanReceiveMaterials = selectedStaticMeshComponent || selectedSkeletalMeshComponent;
    const size_t selectedEntitySlotCount = selectedStaticMeshComponent     ? selectedStaticMeshComponent->getMaterialSlotCount()
                                           : selectedSkeletalMeshComponent ? selectedSkeletalMeshComponent->getMaterialSlotCount()
                                                                           : 0;
    const bool selectedEntitySlotCountMatchesModel = selectedEntitySlotCount == modelAsset->meshes.size();

    auto runMaterialExport = [&](bool applyToSelectedEntity)
    {
        const std::filesystem::path exportPath = std::filesystem::path(m_modelMaterialsExportDirectory);
        if (exportPath.empty())
        {
            m_notificationManager.showError("Output folder is empty");
            return;
        }

        std::vector<std::string> perMeshMaterialPaths;
        std::vector<std::string> *outputBindings = applyToSelectedEntity ? &perMeshMaterialPaths : nullptr;

        if (!exportModelMaterials(assetPath,
                                  exportPath,
                                  textureSearchDirectory,
                                  m_modelTextureManualOverrides,
                                  outputBindings))
        {
            m_notificationManager.showError("Material export failed");
            return;
        }

        if (!applyToSelectedEntity)
            return;

        if (!applyPerMeshMaterialPathsToSelectedEntity(perMeshMaterialPaths))
        {
            m_notificationManager.showWarning("Materials exported, but auto-apply to selected entity failed");
            return;
        }

        m_notificationManager.showSuccess("Exported and applied materials to selected entity");
    };

    auto runApplyMaterialsOnly = [&]()
    {
        auto project = m_currentProject.lock();
        const std::filesystem::path projectRoot = project ? std::filesystem::path(project->fullPath) : std::filesystem::path{};

        std::filesystem::path materialsDirectory = std::filesystem::path(m_modelMaterialsExportDirectory);
        if (materialsDirectory.empty())
        {
            m_notificationManager.showError("Output folder is empty");
            return;
        }

        if (materialsDirectory.is_relative() && !projectRoot.empty())
            materialsDirectory = projectRoot / materialsDirectory;

        materialsDirectory = makeAbsoluteNormalized(materialsDirectory);

        std::vector<std::string> perMeshMaterialPaths;
        size_t matchedSlots = 0;
        if (!buildPerMeshMaterialPathsFromDirectory(*modelAsset, materialsDirectory, perMeshMaterialPaths, matchedSlots))
        {
            m_notificationManager.showError("Failed to resolve materials from output folder");
            return;
        }

        if (!applyPerMeshMaterialPathsToSelectedEntity(perMeshMaterialPaths))
        {
            m_notificationManager.showWarning("Resolved materials, but failed to apply to selected entity");
            return;
        }

        m_notificationManager.showSuccess("Applied " + std::to_string(matchedSlots) + " material slot(s) from output folder");
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

    if (!m_selectedEntity)
        ImGui::TextDisabled("Select a scene entity with mesh component to enable auto-apply.");
    else if (!selectedEntityCanReceiveMaterials)
        ImGui::TextDisabled("Selected entity has no mesh component.");
    else if (!selectedEntitySlotCountMatchesModel)
        ImGui::TextDisabled("Slot mismatch: selected entity has %zu slot(s), model has %zu mesh slot(s).", selectedEntitySlotCount, modelAsset->meshes.size());
    else
        ImGui::TextDisabled("Auto-apply target: '%s' (%zu slot(s)).", m_selectedEntity->getName().c_str(), selectedEntitySlotCount);

    ImGui::TextWrapped("Set a texture search folder, optionally add manual overrides for unresolved paths, then export or apply materials.");
}

bool Editor::applyMaterialToSelectedEntity(const std::string &materialPath, std::optional<size_t> slot, bool forceAllSlots)
{
    if (!m_selectedEntity)
    {
        VX_EDITOR_WARNING_STREAM("Material apply failed. No selected entity.");
        return false;
    }

    auto staticMeshComponent = m_selectedEntity->getComponent<engine::StaticMeshComponent>();
    auto skeletalMeshComponent = m_selectedEntity->getComponent<engine::SkeletalMeshComponent>();

    if (!staticMeshComponent && !skeletalMeshComponent)
    {
        VX_EDITOR_WARNING_STREAM("Material apply failed for entity '" << m_selectedEntity->getName() << "'. Entity has no mesh component.");
        return false;
    }

    auto material = ensureMaterialLoaded(materialPath);
    if (!material)
    {
        VX_EDITOR_ERROR_STREAM("Material apply failed. Could not load material: " << materialPath);
        return false;
    }

    const size_t slotCount = staticMeshComponent ? staticMeshComponent->getMaterialSlotCount() : skeletalMeshComponent->getMaterialSlotCount();
    std::optional<size_t> resolvedSlot = slot;

    if (forceAllSlots)
        resolvedSlot.reset();
    else if (!resolvedSlot.has_value() && m_selectedMeshSlot.has_value() && m_selectedMeshSlot.value() < slotCount)
        resolvedSlot = static_cast<size_t>(m_selectedMeshSlot.value());

    if (resolvedSlot.has_value())
    {
        if (resolvedSlot.value() >= slotCount)
        {
            VX_EDITOR_WARNING_STREAM("Material apply failed. Slot " << resolvedSlot.value() << " is out of range for entity '" << m_selectedEntity->getName() << "'.");
            return false;
        }

        if (staticMeshComponent)
        {
            staticMeshComponent->setMaterialOverride(resolvedSlot.value(), material);
            staticMeshComponent->setMaterialOverridePath(resolvedSlot.value(), materialPath);
        }
        else
        {
            skeletalMeshComponent->setMaterialOverride(resolvedSlot.value(), material);
            skeletalMeshComponent->setMaterialOverridePath(resolvedSlot.value(), materialPath);
        }

        VX_EDITOR_INFO_STREAM("Applied material '" << materialPath << "' to entity '" << m_selectedEntity->getName() << "' slot " << resolvedSlot.value());
        return true;
    }

    for (size_t index = 0; index < slotCount; ++index)
    {
        if (staticMeshComponent)
        {
            staticMeshComponent->setMaterialOverride(index, material);
            staticMeshComponent->setMaterialOverridePath(index, materialPath);
        }
        else
        {
            skeletalMeshComponent->setMaterialOverride(index, material);
            skeletalMeshComponent->setMaterialOverridePath(index, materialPath);
        }
    }

    VX_EDITOR_INFO_STREAM("Applied material '" << materialPath << "' to all slots of entity '" << m_selectedEntity->getName() << "'.");
    return true;
}

bool Editor::spawnEntityFromModelAsset(const std::string &assetPath)
{
    if (!m_scene)
    {
        VX_EDITOR_ERROR_STREAM("Spawn model failed. Scene is null.");
        return false;
    }

    const auto *model = ensureModelAssetLoaded(assetPath);
    if (!model)
    {
        VX_EDITOR_ERROR_STREAM("Failed to load model asset: " << assetPath);
        return false;
    }

    const std::filesystem::path path(assetPath);
    const std::string entityName = path.stem().empty() ? "Model" : path.stem().string();

    auto entity = m_scene->addEntity(entityName);

    if (model->skeleton.has_value())
    {
        auto *skeletalMeshComponent = entity->addComponent<engine::SkeletalMeshComponent>(model->meshes, model->skeleton.value());

        if (!model->animations.empty())
        {
            auto *animatorComponent = entity->addComponent<engine::AnimatorComponent>();
            animatorComponent->setAnimations(model->animations, &skeletalMeshComponent->getSkeleton());
            animatorComponent->setSelectedAnimationIndex(0);
        }
    }
    else
        entity->addComponent<engine::StaticMeshComponent>(model->meshes);

    if (auto transform = entity->getComponent<engine::Transform3DComponent>())
    {
        glm::vec3 spawnPosition(0.0f);

        if (m_editorCamera)
            spawnPosition = m_editorCamera->getPosition() + m_editorCamera->getForward() * 3.0f;

        transform->setPosition(spawnPosition);
    }

    setSelectedEntity(entity.get());

    VX_EDITOR_INFO_STREAM("Spawned entity '" << entityName << "' from model: " << assetPath);
    m_notificationManager.showSuccess("Spawned model: " + entityName);

    return true;
}

void Editor::addPrimitiveEntity(const std::string &primitiveName)
{
    if (!m_scene)
    {
        VX_EDITOR_ERROR_STREAM("Add primitive failed. Scene is null.");
        return;
    }

    auto entity = m_scene->addEntity(primitiveName);
    std::vector<engine::CPUMesh> meshes;

    if (primitiveName == "Cube")
    {
        auto mesh = engine::CPUMesh::build<engine::vertex::Vertex3D>(engine::cube::vertices, engine::cube::indices);
        mesh.name = "Cube";
        meshes.push_back(mesh);
    }
    else if (primitiveName == "Sphere")
    {
        std::vector<engine::vertex::Vertex3D> vertices;
        std::vector<uint32_t> indices;
        engine::circle::genereteVerticesAndIndices(vertices, indices);
        auto mesh = engine::CPUMesh::build<engine::vertex::Vertex3D>(vertices, indices);
        mesh.name = "Sphere";
        meshes.push_back(mesh);
    }

    if (!meshes.empty())
    {
        entity->addComponent<engine::StaticMeshComponent>(meshes);

        if (auto transform = entity->getComponent<engine::Transform3DComponent>())
        {
            glm::vec3 spawnPosition(0.0f);

            if (m_editorCamera)
                spawnPosition = m_editorCamera->getPosition() + m_editorCamera->getForward() * 3.0f;

            transform->setPosition(spawnPosition);
        }
    }

    setSelectedEntity(entity.get());

    VX_EDITOR_INFO_STREAM("Added primitive entity: " << primitiveName);
    m_notificationManager.showSuccess("Added primitive: " + primitiveName);
}

void Editor::addEmptyEntity(const std::string &name)
{
    if (!m_scene)
    {
        VX_EDITOR_ERROR_STREAM("Add empty entity failed. Scene is null.");
        return;
    }

    auto entity = m_scene->addEntity(name.empty() ? "Empty" : name);
    if (!entity)
    {
        VX_EDITOR_ERROR_STREAM("Failed to create empty entity.");
        return;
    }

    if (auto transform = entity->getComponent<engine::Transform3DComponent>())
    {
        glm::vec3 spawnPosition(0.0f);
        if (m_editorCamera)
            spawnPosition = m_editorCamera->getPosition() + m_editorCamera->getForward() * 3.0f;

        transform->setPosition(spawnPosition);
    }

    setSelectedEntity(entity.get());
    VX_EDITOR_INFO_STREAM("Added empty entity: " << entity->getName());
    m_notificationManager.showSuccess("Added empty entity");
}

engine::Camera::SharedPtr Editor::getCurrentCamera()
{
    return m_editorCamera;
}

void Editor::addOnViewportChangedCallback(const std::function<void(uint32_t width, uint32_t height)> &function)
{
    m_onViewportWindowResized.push_back(function);
}

void Editor::drawViewport(VkDescriptorSet viewportDescriptorSet)
{
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_None);

    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    ImGui::Image(viewportDescriptorSet, ImVec2(viewportPanelSize.x, viewportPanelSize.y));
    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageMax = ImGui::GetItemRectMax();
    const bool imageHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (imageHovered && ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            std::string droppedPath((const char *)payload->Data, payload->DataSize - 1);
            std::string extension = std::filesystem::path(droppedPath).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
            const std::filesystem::path droppedAssetPath = std::filesystem::path(droppedPath);

            if (extension == ".elixmat")
            {
                if (applyMaterialToSelectedEntity(droppedPath))
                    m_notificationManager.showSuccess("Applied material to selected mesh");
                else
                    m_notificationManager.showWarning("Select a mesh entity to apply this material");
            }
            else if (isModelAssetPath(droppedAssetPath))
            {
                if (spawnEntityFromModelAsset(droppedPath))
                    m_notificationManager.showSuccess("Model spawned in scene");
                else
                    m_notificationManager.showError("Failed to spawn model");
            }
            else if (isTextureAssetPath(droppedAssetPath) || extension == ".hdr" || extension == ".exr")
            {
                if (m_scene)
                {
                    m_scene->setSkyboxHDRPath(droppedPath);
                    m_notificationManager.showSuccess("Skybox applied to scene");
                    VX_EDITOR_INFO_STREAM("Set scene skybox path: " << droppedPath << '\n');
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    constexpr float rightMouseDragThresholdPx = 4.0f;
    constexpr double rightMouseHoldToCaptureSec = 0.12;

    if (imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !m_isViewportMouseCaptured && !ImGui::IsPopupOpen("ViewportContextMenu"))
    {
        m_isViewportRightMousePendingContext = true;
        m_viewportRightMouseDownTime = ImGui::GetTime();

        const ImVec2 mousePos = ImGui::GetMousePos();
        m_viewportRightMouseDownX = mousePos.x;
        m_viewportRightMouseDownY = mousePos.y;
    }

    bool shouldBeginViewportMouseCapture = false;

    if (m_isViewportRightMousePendingContext && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        const ImVec2 mousePos = ImGui::GetMousePos();
        const float dx = mousePos.x - m_viewportRightMouseDownX;
        const float dy = mousePos.y - m_viewportRightMouseDownY;
        const float dragDistanceSq = dx * dx + dy * dy;
        const double heldSec = ImGui::GetTime() - m_viewportRightMouseDownTime;

        if (dragDistanceSq > (rightMouseDragThresholdPx * rightMouseDragThresholdPx) || heldSec >= rightMouseHoldToCaptureSec)
        {
            shouldBeginViewportMouseCapture = true;
            m_isViewportRightMousePendingContext = false;
        }
    }

    if (m_isViewportRightMousePendingContext && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool releasedInsideViewportImage = mouse.x >= imageMin.x && mouse.x < imageMax.x &&
                                                 mouse.y >= imageMin.y && mouse.y < imageMax.y;

        if (releasedInsideViewportImage && !m_isViewportMouseCaptured)
            ImGui::OpenPopup("ViewportContextMenu");

        m_isViewportRightMousePendingContext = false;
    }

    if (ImGui::BeginPopup("ViewportContextMenu"))
    {
        ImGui::TextUnformatted("Create");
        ImGui::Separator();

        if (ImGui::MenuItem("Cube"))
        {
            addPrimitiveEntity("Cube");
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::MenuItem("Sphere"))
        {
            addPrimitiveEntity("Sphere");
            ImGui::CloseCurrentPopup();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Environment");

        if (m_scene && m_scene->hasSkyboxHDR())
        {
            ImGui::TextWrapped("Skybox: %s", m_scene->getSkyboxHDRPath().c_str());
            if (ImGui::MenuItem("Clear Skybox HDR"))
            {
                m_scene->clearSkyboxHDR();
                m_notificationManager.showInfo("Cleared scene skybox HDR");
                VX_EDITOR_INFO_STREAM("Cleared scene skybox HDR\n");
                ImGui::CloseCurrentPopup();
            }
        }
        else
            ImGui::TextDisabled("Skybox: <None>");

        ImGui::EndPopup();
    }

    drawGuizmo();

    uint32_t x = static_cast<uint32_t>(viewportPanelSize.x);
    uint32_t y = static_cast<uint32_t>(viewportPanelSize.y);

    bool sizeChanged = (m_viewportSizeX != x || m_viewportSizeY != y);

    if (sizeChanged)
    {
        m_viewportSizeX = x;
        m_viewportSizeY = y;

        for (const auto &function : m_onViewportWindowResized)
            if (function)
                function(m_viewportSizeX, m_viewportSizeY);
    }

    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool viewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGuiIO &io = ImGui::GetIO();

    if (viewportFocused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        auto project = m_currentProject.lock();
        if (m_scene && project)
        {
            m_scene->saveSceneToFile(project->entryScene);
            m_notificationManager.showInfo("Scene saved");
            VX_EDITOR_INFO_STREAM("Scene saved to: " << project->entryScene);
        }
    }

    auto &window = core::VulkanContext::getContext()->getSwapchain()->getWindow();
    GLFWwindow *windowHandler = window.getRawHandler();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() &&
        !m_isColliderHandleHovered && !m_isColliderHandleActive &&
        m_viewportSizeX > 0 && m_viewportSizeY > 0)
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float imageWidth = imageMax.x - imageMin.x;
        const float imageHeight = imageMax.y - imageMin.y;

        if (imageWidth > 0.0f && imageHeight > 0.0f &&
            mouse.x >= imageMin.x && mouse.x < imageMax.x &&
            mouse.y >= imageMin.y && mouse.y < imageMax.y)
        {
            const float u = std::clamp((mouse.x - imageMin.x) / imageWidth, 0.0f, 0.999999f);
            const float v = std::clamp((mouse.y - imageMin.y) / imageHeight, 0.0f, 0.999999f);

            m_pendingPickX = static_cast<uint32_t>(u * static_cast<float>(m_viewportSizeX));
            m_pendingPickY = static_cast<uint32_t>(v * static_cast<float>(m_viewportSizeY));
            m_hasPendingObjectPick = true;
        }
    }

    if (m_selectedEntity && ImGui::IsKeyPressed(ImGuiKey_F, false))
        focusSelectedEntity();

    const bool shouldCaptureViewportMouse = !ImGui::IsPopupOpen("ViewportContextMenu") &&
                                            (m_isViewportMouseCaptured
                                                 ? ImGui::IsMouseDown(ImGuiMouseButton_Right)
                                                 : shouldBeginViewportMouseCapture);

    if (shouldCaptureViewportMouse && !m_isViewportMouseCaptured)
    {
        m_isViewportRightMousePendingContext = false;
        glfwGetCursorPos(windowHandler, &m_capturedMouseRestoreX, &m_capturedMouseRestoreY);
        glfwSetInputMode(windowHandler, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

#if defined(GLFW_RAW_MOUSE_MOTION)
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(windowHandler, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
#endif

        m_isViewportMouseCaptured = true;
    }
    else if (!shouldCaptureViewportMouse && m_isViewportMouseCaptured)
    {
#if defined(GLFW_RAW_MOUSE_MOTION)
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(windowHandler, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
#endif

        glfwSetInputMode(windowHandler, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetCursorPos(windowHandler, m_capturedMouseRestoreX, m_capturedMouseRestoreY);
        m_isViewportMouseCaptured = false;
    }

    if (m_isViewportMouseCaptured)
    {
        glm::vec2 mouseDelta(io.MouseDelta.x, io.MouseDelta.y);

        float yaw = m_editorCamera->getYaw();
        float pitch = m_editorCamera->getPitch();

        yaw += mouseDelta.x * m_mouseSensitivity;
        pitch -= mouseDelta.y * m_mouseSensitivity;

        m_editorCamera->setYaw(yaw);
        m_editorCamera->setPitch(pitch);

        float velocity = m_movementSpeed * io.DeltaTime;
        glm::vec3 position = m_editorCamera->getPosition();

        const glm::vec3 forward = m_editorCamera->getForward();
        const glm::vec3 right = glm::normalize(glm::cross(forward, m_editorCamera->getUp()));

        if (ImGui::IsKeyDown(ImGuiKey_W))
            position += forward * velocity;
        if (ImGui::IsKeyDown(ImGuiKey_S))
            position -= forward * velocity;
        if (ImGui::IsKeyDown(ImGuiKey_A))
            position -= right * velocity;
        if (ImGui::IsKeyDown(ImGuiKey_D))
            position += right * velocity;
        if (ImGui::IsKeyDown(ImGuiKey_E))
            position += m_editorCamera->getUp() * velocity;
        if (ImGui::IsKeyDown(ImGuiKey_Q))
            position -= m_editorCamera->getUp() * velocity;

        m_editorCamera->setPosition(position);
        m_editorCamera->updateCameraVectors();

        io.WantCaptureMouse = true;
        io.WantCaptureKeyboard = true;
    }

    ImGui::End();
}

void Editor::processPendingObjectSelection()
{
    if (!m_hasPendingObjectPick || !m_objectIdColorImage || !m_scene || !m_entityIdBuffer)
        return;

    auto image = m_objectIdColorImage->getImage();

    auto commandBuffer = core::CommandBuffer::createShared(*core::VulkanContext::getContext()->getGraphicsCommandPool());
    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    const VkImageSubresourceRange subresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    engine::utilities::ImageUtilities::insertImageMemoryBarrier(
        *image,
        *commandBuffer,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        subresourceRange);

    engine::utilities::ImageUtilities::copyImageToBuffer(
        *image,
        *m_entityIdBuffer,
        *commandBuffer,
        {static_cast<int32_t>(m_pendingPickX), static_cast<int32_t>(m_pendingPickY), 0});

    engine::utilities::ImageUtilities::insertImageMemoryBarrier(
        *image,
        *commandBuffer,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        subresourceRange);

    commandBuffer->end();

    VkFenceCreateInfo fenceCreateInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence readbackFence = VK_NULL_HANDLE;
    if (vkCreateFence(core::VulkanContext::getContext()->getDevice(), &fenceCreateInfo, nullptr, &readbackFence) != VK_SUCCESS)
    {
        m_hasPendingObjectPick = false;
        return;
    }

    if (!commandBuffer->submit(core::VulkanContext::getContext()->getGraphicsQueue(), {}, {}, {}, readbackFence))
    {
        vkDestroyFence(core::VulkanContext::getContext()->getDevice(), readbackFence, nullptr);
        m_hasPendingObjectPick = false;
        return;
    }

    if (vkWaitForFences(core::VulkanContext::getContext()->getDevice(), 1, &readbackFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
    {
        vkDestroyFence(core::VulkanContext::getContext()->getDevice(), readbackFence, nullptr);
        m_hasPendingObjectPick = false;
        return;
    }

    vkDestroyFence(core::VulkanContext::getContext()->getDevice(), readbackFence, nullptr);

    uint32_t *data = nullptr;
    m_entityIdBuffer->map(reinterpret_cast<void *&>(data));

    const uint32_t selectedObjectId = data ? data[0] : 0u;
    m_entityIdBuffer->unmap();

    if (selectedObjectId == engine::render::OBJECT_ID_NONE)
    {
        setSelectedEntity(nullptr);
        m_selectedMeshSlot.reset();
        m_hasPendingObjectPick = false;
        return;
    }

    const uint32_t encodedEntityId = engine::render::decodeEntityEncoded(selectedObjectId);
    const uint32_t encodedMeshSlot = engine::render::decodeMeshEncoded(selectedObjectId);

    if (encodedEntityId == 0u)
    {
        setSelectedEntity(nullptr);
        m_selectedMeshSlot.reset();
        m_hasPendingObjectPick = false;
        return;
    }

    engine::Entity *pickedEntity = m_scene->getEntityById(encodedEntityId - 1u);
    setSelectedEntity(pickedEntity);

    if (!pickedEntity || encodedMeshSlot == 0u)
    {
        m_selectedMeshSlot.reset();
        m_hasPendingObjectPick = false;
        return;
    }

    const uint32_t decodedMeshSlot = encodedMeshSlot - 1u;

    size_t meshSlotCount = 0;

    if (auto staticMeshComponent = pickedEntity->getComponent<engine::StaticMeshComponent>())
        meshSlotCount = staticMeshComponent->getMaterialSlotCount();
    else if (auto skeletalMeshComponent = pickedEntity->getComponent<engine::SkeletalMeshComponent>())
        meshSlotCount = skeletalMeshComponent->getMaterialSlotCount();

    if (decodedMeshSlot < meshSlotCount)
        m_selectedMeshSlot = decodedMeshSlot;
    else
        m_selectedMeshSlot.reset();

    m_hasPendingObjectPick = false;
}

void Editor::drawAssets()
{
    if (!m_showAssetsWindow || !m_assetsWindow)
        return;

    ImGui::Begin("Assets");

    if (!m_currentProject.lock())
    {
        ImGui::End();
        return;
    }

    m_assetsWindow->draw();

    ImGui::End();
}

ELIX_NESTED_NAMESPACE_END
