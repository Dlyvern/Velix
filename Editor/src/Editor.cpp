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
#include "Engine/Components/CharacterMovementComponent.hpp"
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
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Assets/ElixPacket.hpp"

#include "Editor/FileHelper.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_node_editor.h>
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
#include <chrono>
#include <iomanip>
#include <limits>
#include <cmath>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <cstdint>
#include <cstdlib>

#include "ImGuizmo.h"
#include <nlohmann/json.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

namespace ed = ax::NodeEditor;

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

    template <typename TValue>
    void hashCombine(size_t &seed, const TValue &value)
    {
        seed ^= std::hash<TValue>{}(value) + 0x9e3779b97f4a7c15ULL + (seed << 6u) + (seed >> 2u);
    }

    size_t hashRenderQualitySettings(const elix::engine::RenderQualitySettings &settings)
    {
        size_t seed = 0;

        hashCombine(seed, static_cast<uint32_t>(settings.shadowQuality));
        hashCombine(seed, static_cast<uint32_t>(settings.shadowCascadeCount));
        hashCombine(seed, settings.shadowMaxDistance);
        hashCombine(seed, settings.enableVSync);
        hashCombine(seed, settings.enablePostProcessing);
        hashCombine(seed, settings.enableRayTracing);
        hashCombine(seed, settings.enableRTShadows);
        hashCombine(seed, settings.enableRTReflections);
        hashCombine(seed, settings.enableRTAO);
        hashCombine(seed, settings.rtaoRadius);
        hashCombine(seed, settings.rtaoSamples);
        hashCombine(seed, static_cast<uint32_t>(settings.rayTracingMode));
        hashCombine(seed, settings.rtShadowSamples);
        hashCombine(seed, settings.rtShadowPenumbraSize);
        hashCombine(seed, settings.rtReflectionSamples);
        hashCombine(seed, settings.rtRoughnessThreshold);
        hashCombine(seed, settings.rtReflectionStrength);
        hashCombine(seed, settings.enableFXAA);
        hashCombine(seed, settings.enableBloom);
        hashCombine(seed, settings.bloomThreshold);
        hashCombine(seed, settings.bloomKnee);
        hashCombine(seed, settings.bloomStrength);
        hashCombine(seed, settings.renderScale);
        hashCombine(seed, static_cast<uint32_t>(settings.anisotropyMode));
        hashCombine(seed, settings.enableSmallFeatureCulling);
        hashCombine(seed, settings.smallFeatureCullingThreshold);
        hashCombine(seed, settings.enableSSAO);
        hashCombine(seed, settings.ssaoRadius);
        hashCombine(seed, settings.ssaoBias);
        hashCombine(seed, settings.ssaoStrength);
        hashCombine(seed, settings.ssaoSamples);
        hashCombine(seed, settings.enableGTAO);
        hashCombine(seed, settings.gtaoDirections);
        hashCombine(seed, settings.gtaoSteps);
        hashCombine(seed, settings.useBentNormals);
        hashCombine(seed, settings.shadowAmbientStrength);
        hashCombine(seed, settings.enableTAA);
        hashCombine(seed, settings.enableSMAA);
        hashCombine(seed, settings.enableCMAA);
        hashCombine(seed, settings.enableColorGrading);
        hashCombine(seed, settings.colorGradingSaturation);
        hashCombine(seed, settings.colorGradingContrast);
        hashCombine(seed, settings.colorGradingTemperature);
        hashCombine(seed, settings.colorGradingTint);
        hashCombine(seed, settings.enableContactShadows);
        hashCombine(seed, settings.contactShadowLength);
        hashCombine(seed, settings.contactShadowStrength);
        hashCombine(seed, settings.contactShadowSteps);
        hashCombine(seed, settings.enableVignette);
        hashCombine(seed, settings.vignetteStrength);
        hashCombine(seed, settings.enableFilmGrain);
        hashCombine(seed, settings.filmGrainStrength);
        hashCombine(seed, settings.enableChromaticAberration);
        hashCombine(seed, settings.chromaticAberrationStrength);

        return seed;
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

    int buildArtifactPreference(const std::filesystem::path &path)
    {
        auto toLower = [](std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                           { return static_cast<char>(std::tolower(character)); });
            return value;
        };

        for (const auto &segment : path)
        {
            const std::string loweredSegment = toLower(segment.string());
            if (loweredSegment == "release")
                return 0;
            if (loweredSegment == "relwithdebinfo")
                return 1;
            if (loweredSegment == "minsizerel")
                return 2;
            if (loweredSegment == "debug")
                return 4;
        }

        // Unknown/mixed folders (including single-config builds).
        return 3;
    }

    std::filesystem::path findGameModuleLibraryPath(const std::filesystem::path &buildDirectory)
    {
        if (buildDirectory.empty() || !std::filesystem::exists(buildDirectory))
            return {};

        const std::vector<std::filesystem::path> searchRoots = {
            buildDirectory / "Release",
            buildDirectory / "RelWithDebInfo",
            buildDirectory / "MinSizeRel",
            buildDirectory,
            buildDirectory / "Debug",
        };

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

        std::filesystem::path bestMatch;
        int bestPreference = std::numeric_limits<int>::max();

        for (const auto &entry : std::filesystem::recursive_directory_iterator(buildDirectory))
        {
            if (!entry.is_regular_file())
                continue;

            const auto path = entry.path();
            if (path.extension() != SHARED_LIB_EXTENSION)
                continue;

            const std::string stem = path.stem().string();
            if (stem == "GameModule" || stem == "libGameModule")
            {
                const int candidatePreference = buildArtifactPreference(path);
                if (candidatePreference < bestPreference ||
                    (candidatePreference == bestPreference && path.string() < bestMatch.string()))
                {
                    bestPreference = candidatePreference;
                    bestMatch = path;
                }
                continue;
            }

            if (path.filename().string().find("GameModule") != std::string::npos)
            {
                const int candidatePreference = buildArtifactPreference(path);
                if (candidatePreference < bestPreference ||
                    (candidatePreference == bestPreference && path.string() < bestMatch.string()))
                {
                    bestPreference = candidatePreference;
                    bestMatch = path;
                }
            }
        }

        return bestMatch;
    }

    std::filesystem::path findPreferredRuntimeExecutableForExport(const std::filesystem::path &runningExecutablePath)
    {
        if (runningExecutablePath.empty())
            return {};

        auto toLower = [](std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                           { return static_cast<char>(std::tolower(character)); });
            return value;
        };

        auto isExistingFile = [](const std::filesystem::path &path) -> bool
        {
            if (path.empty())
                return false;

            std::error_code errorCode;
            return std::filesystem::exists(path, errorCode) &&
                   std::filesystem::is_regular_file(path, errorCode);
        };

        std::vector<std::filesystem::path> candidates;
        auto addCandidate = [&](const std::filesystem::path &candidate)
        {
            if (candidate.empty())
                return;

            if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
                candidates.push_back(candidate);
        };

        const std::filesystem::path executableFileName = runningExecutablePath.filename();
        const std::filesystem::path executableDirectory = runningExecutablePath.parent_path();
        const std::string directoryNameLower = toLower(executableDirectory.filename().string());

        auto addConfigCandidates = [&](const std::filesystem::path &baseDirectory)
        {
            addCandidate(baseDirectory / "Release" / executableFileName);
            addCandidate(baseDirectory / "RelWithDebInfo" / executableFileName);
            addCandidate(baseDirectory / "MinSizeRel" / executableFileName);
            addCandidate(baseDirectory / "release" / executableFileName);
            addCandidate(baseDirectory / "relwithdebinfo" / executableFileName);
            addCandidate(baseDirectory / "minsizerel" / executableFileName);
        };

        if (directoryNameLower == "debug" || directoryNameLower == "release" ||
            directoryNameLower == "relwithdebinfo" || directoryNameLower == "minsizerel")
            addConfigCandidates(executableDirectory.parent_path());
        else
            addConfigCandidates(executableDirectory);

        addCandidate(runningExecutablePath);

        for (const auto &candidate : candidates)
        {
            if (isExistingFile(candidate))
                return candidate;
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

Editor::~Editor()
{
    for (auto &[materialPath, state] : m_materialEditorUiState)
    {
        (void)materialPath;
        if (state.nodeEditorContext)
        {
            ed::DestroyEditor(state.nodeEditorContext);
            state.nodeEditorContext = nullptr;
            state.nodeEditorInitialized = false;
        }
    }
}

void Editor::setScene(engine::Scene::SharedPtr scene)
{
    m_scene = std::move(scene);
    m_terrainTools.setScene(m_scene);
    m_selectedEntity = nullptr;
    m_selectedMeshSlot.reset();
    m_hasPendingObjectPick = false;
    clearSelectedUIElement();
    resetSceneActionHistory();
    restoreSceneMaterialOverrides();
}

void Editor::restoreSceneMaterialOverrides()
{
    if (!m_scene)
        return;

    auto project = m_currentProject.lock();
    if (!project)
        return;

    for (const auto &entity : m_scene->getEntities())
    {
        if (!entity)
            continue;

        if (auto *staticMeshComponent = entity->getComponent<engine::StaticMeshComponent>())
        {
            const size_t slotCount = staticMeshComponent->getMaterialSlotCount();
            for (size_t slot = 0; slot < slotCount; ++slot)
            {
                if (staticMeshComponent->getMaterialOverride(slot))
                    continue;

                const std::string &overridePath = staticMeshComponent->getMaterialOverridePath(slot);
                if (overridePath.empty())
                    continue;

                auto material = ensureMaterialLoaded(overridePath);
                if (material)
                    staticMeshComponent->setMaterialOverride(slot, material);
            }
        }

        if (auto *skeletalMeshComponent = entity->getComponent<engine::SkeletalMeshComponent>())
        {
            const size_t slotCount = skeletalMeshComponent->getMaterialSlotCount();
            for (size_t slot = 0; slot < slotCount; ++slot)
            {
                if (skeletalMeshComponent->getMaterialOverride(slot))
                    continue;

                const std::string &overridePath = skeletalMeshComponent->getMaterialOverridePath(slot);
                if (overridePath.empty())
                    continue;

                auto material = ensureMaterialLoaded(overridePath);
                if (material)
                    skeletalMeshComponent->setMaterialOverride(slot, material);
            }
        }
    }
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
    auto *characterMovementComponent = m_selectedEntity->getComponent<engine::CharacterMovementComponent>();

    if ((collisionComponent || characterMovementComponent) && m_showCollisionBounds && m_editorCamera)
    {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const glm::mat4 view = m_editorCamera->getViewMatrix();
        const glm::mat4 projection = m_editorCamera->getProjectionMatrix();
        const glm::vec3 worldPosition = tc->getWorldPosition();
        const glm::quat worldRotation = tc->getWorldRotation();
        glm::mat4 colliderMatrix = composeTransform(worldPosition, worldRotation);
        if (collisionComponent && collisionComponent->getShape())
            colliderMatrix *= shapeLocalPoseToMatrix(collisionComponent->getShape());

        const glm::vec3 center = transformPoint(colliderMatrix, glm::vec3(0.0f));
        const glm::vec3 axisX = transformDirection(colliderMatrix, glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::vec3 axisY = transformDirection(colliderMatrix, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 axisZ = transformDirection(colliderMatrix, glm::vec3(0.0f, 0.0f, 1.0f));

        constexpr ImU32 colliderLineColor = IM_COL32(75, 215, 255, 220);
        constexpr ImU32 boxAxisXColor = IM_COL32(245, 95, 95, 235);
        constexpr ImU32 boxAxisYColor = IM_COL32(95, 235, 120, 235);
        constexpr ImU32 boxAxisZColor = IM_COL32(110, 160, 255, 235);

        const bool drawCollisionBox = collisionComponent &&
                                      collisionComponent->getShapeType() == engine::CollisionComponent::ShapeType::BOX;

        if (drawCollisionBox)
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
                std::pair<int, int>{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

            for (const auto &[from, to] : edges)
                drawLine3D(drawList, worldCorners[from], worldCorners[to], view, projection, viewportPos, viewportSize, colliderLineColor, 1.5f);

            const glm::vec3 handleX = center + axisX * halfExtents.x;
            const glm::vec3 handleY = center + axisY * halfExtents.y;
            const glm::vec3 handleZ = center + axisZ * halfExtents.z;

            colliderHandles.push_back(makeHandleProjection(static_cast<int>(ColliderHandleType::BOX_X), center, handleX, view, projection, viewportPos, viewportSize));
            colliderHandles.push_back(makeHandleProjection(static_cast<int>(ColliderHandleType::BOX_Y), center, handleY, view, projection, viewportPos, viewportSize));
            colliderHandles.push_back(makeHandleProjection(static_cast<int>(ColliderHandleType::BOX_Z), center, handleZ, view, projection, viewportPos, viewportSize));
        }
        else
        {
            const bool useCharacterMovementCapsule = characterMovementComponent &&
                                                     (!collisionComponent || collisionComponent->getShapeType() != engine::CollisionComponent::ShapeType::CAPSULE);

            const float radius = useCharacterMovementCapsule
                                     ? characterMovementComponent->getCapsuleRadius()
                                     : collisionComponent->getCapsuleRadius();
            const float halfHeight = useCharacterMovementCapsule
                                         ? (characterMovementComponent->getCapsuleHeight() * 0.5f)
                                         : collisionComponent->getCapsuleHalfHeight();

            const glm::vec3 capsuleAxis = useCharacterMovementCapsule ? axisY : axisX;
            const glm::vec3 radiusAxisA = useCharacterMovementCapsule ? axisX : axisY;
            const glm::vec3 radiusAxisB = axisZ;

            const glm::vec3 topCenter = center + capsuleAxis * halfHeight;
            const glm::vec3 bottomCenter = center - capsuleAxis * halfHeight;

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

            drawCircle(topCenter, radiusAxisA, radiusAxisB, radius);
            drawCircle(bottomCenter, radiusAxisA, radiusAxisB, radius);
            drawCircle(center, radiusAxisA, radiusAxisB, radius);

            drawLine3D(drawList, topCenter + radiusAxisA * radius, bottomCenter + radiusAxisA * radius, view, projection, viewportPos, viewportSize, colliderLineColor, 1.3f);
            drawLine3D(drawList, topCenter - radiusAxisA * radius, bottomCenter - radiusAxisA * radius, view, projection, viewportPos, viewportSize, colliderLineColor, 1.3f);
            drawLine3D(drawList, topCenter + radiusAxisB * radius, bottomCenter + radiusAxisB * radius, view, projection, viewportPos, viewportSize, colliderLineColor, 1.3f);
            drawLine3D(drawList, topCenter - radiusAxisB * radius, bottomCenter - radiusAxisB * radius, view, projection, viewportPos, viewportSize, colliderLineColor, 1.3f);

            const glm::vec3 radiusHandleY = center + radiusAxisA * radius;
            const glm::vec3 radiusHandleZ = center + radiusAxisB * radius;
            const glm::vec3 heightHandle = center + capsuleAxis * (halfHeight + radius);

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
                if (collisionComponent)
                    m_colliderDragStartBoxHalfExtents = collisionComponent->getBoxHalfExtents();

                if (characterMovementComponent && (!collisionComponent || collisionComponent->getShapeType() != engine::CollisionComponent::ShapeType::CAPSULE))
                {
                    m_colliderDragStartCapsuleRadius = characterMovementComponent->getCapsuleRadius();
                    m_colliderDragStartCapsuleHalfHeight = characterMovementComponent->getCapsuleHeight() * 0.5f;
                }
                else if (collisionComponent)
                {
                    m_colliderDragStartCapsuleRadius = collisionComponent->getCapsuleRadius();
                    m_colliderDragStartCapsuleHalfHeight = collisionComponent->getCapsuleHalfHeight();
                }
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
                    if (collisionComponent)
                        collisionComponent->setBoxHalfExtents(nextHalfExtents);
                    break;
                }
                case ColliderHandleType::BOX_Y:
                {
                    glm::vec3 nextHalfExtents = m_colliderDragStartBoxHalfExtents;
                    nextHalfExtents.y = std::max(0.01f, nextHalfExtents.y + worldDelta);
                    if (collisionComponent)
                        collisionComponent->setBoxHalfExtents(nextHalfExtents);
                    break;
                }
                case ColliderHandleType::BOX_Z:
                {
                    glm::vec3 nextHalfExtents = m_colliderDragStartBoxHalfExtents;
                    nextHalfExtents.z = std::max(0.01f, nextHalfExtents.z + worldDelta);
                    if (collisionComponent)
                        collisionComponent->setBoxHalfExtents(nextHalfExtents);
                    break;
                }
                case ColliderHandleType::CAPSULE_RADIUS_Y:
                case ColliderHandleType::CAPSULE_RADIUS_Z:
                {
                    const float nextRadius = std::max(0.01f, m_colliderDragStartCapsuleRadius + worldDelta);
                    if (characterMovementComponent && (!collisionComponent || collisionComponent->getShapeType() != engine::CollisionComponent::ShapeType::CAPSULE))
                        characterMovementComponent->setCapsule(nextRadius, m_colliderDragStartCapsuleHalfHeight * 2.0f);
                    else if (collisionComponent)
                        collisionComponent->setCapsuleDimensions(nextRadius, m_colliderDragStartCapsuleHalfHeight);
                    break;
                }
                case ColliderHandleType::CAPSULE_HEIGHT:
                {
                    const float nextHalfHeight = std::max(0.0f, m_colliderDragStartCapsuleHalfHeight + worldDelta);
                    if (characterMovementComponent && (!collisionComponent || collisionComponent->getShapeType() != engine::CollisionComponent::ShapeType::CAPSULE))
                        characterMovementComponent->setCapsule(m_colliderDragStartCapsuleRadius, nextHalfHeight * 2.0f);
                    else if (collisionComponent)
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
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.DockingSeparatorSize = 2.0f;

    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.WindowPadding = ImVec2(10, 10);
    style.CellPadding = ImVec2(6, 4);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

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
    colors[ImGuiCol_TabActive] = ImVec4(0.160f, 0.172f, 0.192f, 1.000f);
    colors[ImGuiCol_TabUnfocused] = bg1;
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.140f, 0.150f, 0.168f, 1.000f);
    colors[ImGuiCol_TabSelectedOverline] = accentBlue;
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(accentBlue.x, accentBlue.y, accentBlue.z, 0.4f);

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
    if (m_pendingSceneOpenRequestCallback)
        m_assetsWindow->setOnSceneOpenRequest(m_pendingSceneOpenRequestCallback);
    m_assetsWindow->setOnAssetSelectionChanged([this](const std::filesystem::path &path)
                                               {
                                                   m_selectedAssetPath = path;
                                                   if (!m_selectedAssetPath.empty())
                                                       m_detailsContext = DetailsContext::Asset;
                                                   else if (m_selectedEntity)
                                                       m_detailsContext = DetailsContext::Entity; });

    m_assetsWindow->setOnAssetDeleted([this](const std::filesystem::path &deletedPath)
                                      {
                                          // Close any open material editor for this path
                                          const std::string deletedStr = deletedPath.lexically_normal().string();
                                          m_openMaterialEditors.erase(
                                              std::remove_if(m_openMaterialEditors.begin(), m_openMaterialEditors.end(),
                                                  [&deletedStr](const OpenMaterialEditor &e)
                                                  { return e.path.lexically_normal().string() == deletedStr; }),
                                              m_openMaterialEditors.end());

                                          // Clear material overrides on mesh components that reference the deleted asset
                                          if (!m_scene)
                                              return;
                                          for (const auto &entity : m_scene->getEntities())
                                          {
                                              auto *staticMesh = entity->getComponent<engine::StaticMeshComponent>();
                                              if (staticMesh)
                                              {
                                                  for (size_t slot = 0; slot < staticMesh->getMaterialSlotCount(); ++slot)
                                                  {
                                                      const std::string &overridePath = staticMesh->getMaterialOverridePath(slot);
                                                      if (!overridePath.empty())
                                                      {
                                                          const std::string normalizedOverride = std::filesystem::path(overridePath).lexically_normal().string();
                                                          if (normalizedOverride == deletedStr || normalizedOverride.find(deletedStr) != std::string::npos)
                                                              staticMesh->clearMaterialOverride(slot);
                                                      }
                                                  }
                                              }
                                              auto *skeletalMesh = entity->getComponent<engine::SkeletalMeshComponent>();
                                              if (skeletalMesh)
                                              {
                                                  for (size_t slot = 0; slot < skeletalMesh->getMaterialSlotCount(); ++slot)
                                                  {
                                                      const std::string &overridePath = skeletalMesh->getMaterialOverridePath(slot);
                                                      if (!overridePath.empty())
                                                      {
                                                          const std::string normalizedOverride = std::filesystem::path(overridePath).lexically_normal().string();
                                                          if (normalizedOverride == deletedStr || normalizedOverride.find(deletedStr) != std::string::npos)
                                                              skeletalMesh->clearMaterialOverride(slot);
                                                      }
                                                  }
                                              }
                                          }
                                      });

    m_entityIdBuffer = core::Buffer::createShared(sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                  core::memory::MemoryUsage::CPU_TO_GPU);

    auto &window = core::VulkanContext::getContext()->getSwapchain()->getWindow();
    GLFWwindow *windowHandler = window.getRawHandler();

    // glfwSetWindowAttrib(windowHandler, GLFW_DECORATED, !m_isDockingWindowFullscreen);
    if (m_isDockingWindowFullscreen)
    {
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        window.setPosition(0, 0);
        window.setSize(mode->width, mode->height);
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

    if (m_reinitDocking)
    {
        m_reinitDocking = false;

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
        ImGui::DockBuilderDockWindow("UI Tools", assetsDock);

        ImGui::DockBuilderDockWindow("Viewport", dockMainId);
        ImGui::DockBuilderDockWindow("Game Viewport", dockMainId);
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
    const bool uiToolsVisible = m_showUITools;

    if (assetsVisible == m_lastDockedAssetsVisibility &&
        terminalVisible == m_lastDockedTerminalVisibility &&
        uiToolsVisible == m_lastDockedUIToolsVisibility)
        return;

    m_lastDockedAssetsVisibility = assetsVisible;
    m_lastDockedTerminalVisibility = terminalVisible;
    m_lastDockedUIToolsVisibility = uiToolsVisible;

    if (!ImGui::DockBuilderGetNode(m_assetsPanelsDockId))
        return;

    ImGui::DockBuilderRemoveNodeChildNodes(m_assetsPanelsDockId);

    std::vector<const char *> dockWindows;
    if (assetsVisible)
        dockWindows.push_back("Assets");
    if (terminalVisible)
        dockWindows.push_back("Terminal with logs");
    if (uiToolsVisible)
        dockWindows.push_back("UI Tools");

    if (dockWindows.empty())
    {
        ImGui::DockBuilderFinish(m_dockSpaceId);
        return;
    }

    if (dockWindows.size() == 1)
    {
        ImGui::DockBuilderDockWindow(dockWindows[0], m_assetsPanelsDockId);
    }
    else if (dockWindows.size() == 2)
    {
        ImGuiID leftNode = m_assetsPanelsDockId;
        ImGuiID rightNode = ImGui::DockBuilderSplitNode(leftNode, ImGuiDir_Right, 0.5f, nullptr, &leftNode);

        ImGui::DockBuilderDockWindow(dockWindows[0], leftNode);
        ImGui::DockBuilderDockWindow(dockWindows[1], rightNode);
    }
    else
    {
        ImGuiID leftNode = m_assetsPanelsDockId;
        ImGuiID rightGroupNode = ImGui::DockBuilderSplitNode(leftNode, ImGuiDir_Right, 0.66f, nullptr, &leftNode);
        ImGuiID middleNode = rightGroupNode;
        ImGuiID rightNode = ImGui::DockBuilderSplitNode(middleNode, ImGuiDir_Right, 0.5f, nullptr, &middleNode);

        ImGui::DockBuilderDockWindow(dockWindows[0], leftNode);
        ImGui::DockBuilderDockWindow(dockWindows[1], middleNode);
        ImGui::DockBuilderDockWindow(dockWindows[2], rightNode);
    }

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
                                        setProjectScriptsRegister(nullptr, {});
                                    }

                                    project->projectLibrary = engine::PluginLoader::loadLibrary(moduleLibraryPath.string());
                                    if (!project->projectLibrary)
                                    {
                                        setProjectScriptsRegister(nullptr, {});
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

                                            setProjectScriptsRegister(nullptr, {});

                                            VX_EDITOR_ERROR_STREAM("Module loaded but getScriptsRegister was not found: " << moduleLibraryPath << '\n');
                                            m_notificationManager.showError("Module loaded, but script register function is missing");
                                        }
                                        else
                                        {
                                            setProjectScriptsRegister(&getScriptsRegisterFunction(), moduleLibraryPath.string());

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

        if (ImGui::Button("Export Game (.elixpacket)"))
            exportCurrentProjectPacket();

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

            if (ImGui::Button("Create"))
            {
                auto project = m_currentProject.lock();
                if (project)
                {
                    std::filesystem::path scenesDir = project->scenesDir.empty()
                                                          ? std::filesystem::path(project->fullPath) / "Scenes"
                                                          : std::filesystem::path(project->scenesDir);

                    std::error_code ec;
                    std::filesystem::create_directories(scenesDir, ec);

                    const std::string rawName = strlen(sceneBuffer) > 0 ? sceneBuffer : "NewScene";
                    const std::string baseName = std::filesystem::path(rawName).stem().string();

                    std::filesystem::path scenePath = scenesDir / (baseName + ".elixscene");
                    int counter = 1;
                    while (std::filesystem::exists(scenePath))
                        scenePath = scenesDir / (baseName + std::to_string(counter++) + ".elixscene");

                    nlohmann::json sceneJson;
                    sceneJson["name"] = scenePath.stem().string();
                    std::ofstream file(scenePath);
                    if (file.is_open())
                    {
                        file << std::setw(4) << sceneJson << '\n';
                        file.close();
                        if (m_pendingSceneOpenRequestCallback)
                            m_pendingSceneOpenRequestCallback(scenePath);
                    }

                    std::memset(sceneBuffer, 0, sizeof(sceneBuffer));
                    ImGui::CloseCurrentPopup();
                    ImGui::CloseCurrentPopup(); // close FilePopup too
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();

            // ImGui::PopStyleColor(1);
            ImGui::EndPopup();
        }

        if (ImGui::Button("Open scene"))
        {
            m_openScenePopupRequested = true;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Save"))
        {
            if (m_currentMode != EditorMode::EDIT)
            {
                m_notificationManager.showError("Cannot save scene while playing. Stop Play first.");
                VX_EDITOR_WARNING_STREAM("Blocked scene save while not in Edit mode.\n");
            }

            auto project = m_currentProject.lock();
            if (m_scene && project && m_currentMode == EditorMode::EDIT)
            {
                m_scene->saveSceneToFile(project->entryScene);
                m_notificationManager.showInfo("Scene saved");
                VX_EDITOR_INFO_STREAM("Scene saved to: " << project->entryScene);
            }
        }

        if (ImGui::Button("Save As..."))
        {
            ImGui::CloseCurrentPopup();
            ImGui::OpenPopup("SaveAsScenePopup");
        }

        ImGui::Separator();
        if (ImGui::Button("Exit"))
            window.close(); // Ha-ha-ha-ha Kill it slower dumbass
        ImGui::PopStyleColor(1);

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("SaveAsScenePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        static char saveAsBuf[256] = "";
        ImGui::Text("Save scene copy as:");
        ImGui::SetNextItemWidth(300.0f);
        ImGui::InputText("##saveas_name", saveAsBuf, sizeof(saveAsBuf));
        ImGui::TextDisabled("File will be saved in the same directory as the current scene.");

        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(120, 0)) && saveAsBuf[0] != '\0')
        {
            if (m_currentMode != EditorMode::EDIT)
            {
                m_notificationManager.showError("Cannot save scene while playing. Stop Play first.");
                VX_EDITOR_WARNING_STREAM("Blocked scene save-as while not in Edit mode.\n");
            }

            auto project = m_currentProject.lock();
            if (m_scene && project && m_currentMode == EditorMode::EDIT)
            {
                std::string filename = saveAsBuf;
                if (filename.find('.') == std::string::npos)
                    filename += ".elixscene";
                const std::filesystem::path newPath =
                    std::filesystem::path(project->entryScene).parent_path() / filename;
                m_scene->saveSceneToFile(newPath.string());
                m_notificationManager.showSuccess("Scene copy saved: " + newPath.filename().string());
                VX_EDITOR_INFO_STREAM("Scene copy saved to: " << newPath.string());
            }
            saveAsBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            saveAsBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_openScenePopupRequested)
    {
        ImGui::OpenPopup("OpenScenePopup");
        m_openScenePopupRequested = false;
    }

    if (ImGui::BeginPopupModal("OpenScenePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        static std::vector<std::filesystem::path> s_foundScenes;
        static int s_selectedScene = -1;

        if (ImGui::IsWindowAppearing())
        {
            s_foundScenes.clear();
            s_selectedScene = -1;
            auto project = m_currentProject.lock();
            if (project && !project->fullPath.empty())
            {
                std::error_code ec;
                for (const auto &entry : std::filesystem::recursive_directory_iterator(
                         project->fullPath, std::filesystem::directory_options::skip_permission_denied, ec))
                {
                    if (entry.is_regular_file())
                    {
                        const auto ext = entry.path().extension().string();
                        if (ext == ".elixscene" || ext == ".scene")
                            s_foundScenes.push_back(entry.path());
                    }
                }
            }
        }

        ImGui::Text("Select a scene to open:");
        ImGui::Separator();

        auto project = m_currentProject.lock();
        ImGui::BeginChild("SceneList", ImVec2(520, 320), true);
        for (int i = 0; i < static_cast<int>(s_foundScenes.size()); ++i)
        {
            const std::string label = project
                                          ? std::filesystem::relative(s_foundScenes[i], project->fullPath).string()
                                          : s_foundScenes[i].filename().string();
            if (ImGui::Selectable(label.c_str(), s_selectedScene == i, ImGuiSelectableFlags_AllowDoubleClick))
            {
                s_selectedScene = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    if (m_pendingSceneOpenRequestCallback)
                        m_pendingSceneOpenRequestCallback(s_foundScenes[i]);
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        if (s_foundScenes.empty())
            ImGui::TextDisabled("No .elixscene files found in project.");
        ImGui::EndChild();

        ImGui::Spacing();
        const bool canOpen = s_selectedScene >= 0 && s_selectedScene < static_cast<int>(s_foundScenes.size());
        if (!canOpen)
            ImGui::BeginDisabled();
        if (ImGui::Button("Open", ImVec2(120, 0)))
        {
            if (m_pendingSceneOpenRequestCallback)
                m_pendingSceneOpenRequestCallback(s_foundScenes[s_selectedScene]);
            ImGui::CloseCurrentPopup();
        }
        if (!canOpen)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();

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

        if (ImGui::Button("Undo Ctrl+Z"))
            performUndoAction();
        if (ImGui::Button("Redo Ctrl+Y"))
            performRedoAction();

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
        setDockingFullscreen(!m_isDockingWindowFullscreen);

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

bool Editor::hasUnsavedSceneChanges()
{
    auto project = m_currentProject.lock();
    if (!m_scene || !project)
        return false;

    const std::filesystem::path scenePath = project->entryScene;
    if (scenePath.empty() || !std::filesystem::exists(scenePath))
        return true;

    const auto timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::filesystem::path sceneDirectory = scenePath.parent_path();
    if (sceneDirectory.empty())
        sceneDirectory = std::filesystem::current_path();

    // Save temporary snapshot next to the entry scene.
    // This keeps relative path serialization and fallback scene naming stable.
    const std::filesystem::path tempScenePath = sceneDirectory / (scenePath.stem().string() +
                                                                  ".__velix_editor_unsaved_scene_check_" +
                                                                  std::to_string(timestamp));

    auto readFileContents = [](const std::filesystem::path &path, std::string &outText) -> bool
    {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
            return false;

        std::ostringstream stream;
        stream << input.rdbuf();
        outText = stream.str();
        return input.good() || input.eof();
    };

    bool hasChanges = true;

    try
    {
        m_scene->saveSceneToFile(tempScenePath.string());

        std::string savedSceneText;
        std::string currentSceneText;

        if (readFileContents(scenePath, savedSceneText) &&
            readFileContents(tempScenePath, currentSceneText))
        {
            hasChanges = (savedSceneText != currentSceneText);
        }
    }
    catch (const std::exception &exception)
    {
        VX_EDITOR_WARNING_STREAM("Failed to evaluate unsaved scene changes: " << exception.what() << '\n');
        hasChanges = true;
    }
    catch (...)
    {
        VX_EDITOR_WARNING_STREAM("Failed to evaluate unsaved scene changes: unknown error\n");
        hasChanges = true;
    }

    std::error_code removeError;
    std::filesystem::remove(tempScenePath, removeError);

    return hasChanges;
}

void Editor::exportCurrentProjectPacket()
{
    if (m_currentMode != EditorMode::EDIT)
    {
        m_notificationManager.showError("Cannot export while playing. Stop Play first.");
        VX_EDITOR_WARNING_STREAM("Blocked game export while not in Edit mode.\n");
        return;
    }

    auto project = m_currentProject.lock();
    if (!project)
    {
        m_notificationManager.showError("Export failed: no project loaded.");
        VX_EDITOR_ERROR_STREAM("Game export failed: project is not loaded.\n");
        return;
    }

    const std::filesystem::path projectRoot = makeAbsoluteNormalized(project->fullPath);
    if (projectRoot.empty() || !std::filesystem::exists(projectRoot))
    {
        m_notificationManager.showError("Export failed: invalid project path.");
        VX_EDITOR_ERROR_STREAM("Game export failed: invalid project path '" << project->fullPath << "'.\n");
        return;
    }

    if (!m_scene || hasUnsavedSceneChanges())
    {
        m_notificationManager.showWarning("Save scene before exporting game packet.");
        VX_EDITOR_WARNING_STREAM("Game export blocked due to unsaved scene changes.\n");
        return;
    }

    const std::filesystem::path entryScenePath = makeAbsoluteNormalized(project->entryScene);
    if (entryScenePath.empty() || !std::filesystem::exists(entryScenePath))
    {
        m_notificationManager.showError("Export failed: entry scene is missing.");
        VX_EDITOR_ERROR_STREAM("Game export failed: entry scene file not found: " << project->entryScene << '\n');
        return;
    }

    std::filesystem::path exportDirectory = project->exportDir.empty()
                                                ? (projectRoot / "Export")
                                                : makeAbsoluteNormalized(project->exportDir);

    std::error_code createDirectoryError;
    std::filesystem::create_directories(exportDirectory, createDirectoryError);
    if (createDirectoryError)
    {
        m_notificationManager.showError("Export failed: cannot create export directory.");
        VX_EDITOR_ERROR_STREAM("Game export failed: could not create export directory '" << exportDirectory
                                                                                         << "': " << createDirectoryError.message() << '\n');
        return;
    }

    std::string packetBaseName = sanitizeFileStem(project->name.empty()
                                                      ? projectRoot.filename().string()
                                                      : project->name);
    if (packetBaseName.empty())
        packetBaseName = "Game";

    const std::filesystem::path packetPath = exportDirectory / (packetBaseName + ".elixpacket");

    const std::filesystem::path buildDirectory = project->buildDir.empty() ? makeAbsoluteNormalized(projectRoot / "build")
                                                                           : makeAbsoluteNormalized(std::filesystem::path(project->buildDir));
    std::filesystem::path cmakePrefixPath = FileHelper::getExecutablePath();
    if (cmakePrefixPath.filename() == "bin")
        cmakePrefixPath = cmakePrefixPath.parent_path();

    const std::filesystem::path cmakeExecutablePath = resolveCMakeExecutablePath();
    const std::string cmakeCommandToken = makeExecutableCommandToken(cmakeExecutablePath);

    const std::string configureCommand = cmakeCommandToken + " -S " + quoteShellArgument(projectRoot) +
                                         " -B " + quoteShellArgument(buildDirectory) +
                                         " -DCMAKE_PREFIX_PATH=" + quoteShellArgument(cmakePrefixPath) +
                                         " -DCMAKE_BUILD_TYPE=Release" +
                                         " -DCMAKE_EXPORT_COMPILE_COMMANDS=ON";

    const auto [configureResult, configureOutput] = FileHelper::executeCommand(configureCommand);
    if (configureResult != 0)
    {
        m_notificationManager.showError("Export failed: cmake configure error.");
        VX_EDITOR_ERROR_STREAM("Game export configure failed.\n"
                               << configureOutput << '\n');
        return;
    }

    if (!syncProjectCompileCommands(*project))
        VX_EDITOR_WARNING_STREAM("Failed to sync compile_commands.json to project root during export.\n");

    std::string buildCommand = cmakeCommandToken + " --build " + quoteShellArgument(buildDirectory) + " --config Release";
#if defined(__linux__)
    buildCommand += " -j";
#endif
    const auto [buildResult, buildOutput] = FileHelper::executeCommand(buildCommand);
    if (buildResult != 0)
    {
        m_notificationManager.showError("Export failed: project build failed.");
        VX_EDITOR_ERROR_STREAM("Game export build failed.\n"
                               << buildOutput << '\n');
        return;
    }

    const std::filesystem::path moduleLibraryPath = findGameModuleLibraryPath(buildDirectory);
    if (moduleLibraryPath.empty())
    {
        m_notificationManager.showError("Export failed: GameModule library was not found.");
        VX_EDITOR_ERROR_STREAM("Game export failed: GameModule library was not found in build directory '" << buildDirectory << "'.\n");
        return;
    }

    engine::ElixPacketSerializer serializer;
    engine::ElixPacketSerializer::ExportOptions exportOptions;

    exportOptions.excludedDirectories.push_back(projectRoot / ".git");
    exportOptions.excludedDirectories.push_back(projectRoot / ".vscode");
    if (!project->buildDir.empty())
        exportOptions.excludedDirectories.push_back(makeAbsoluteNormalized(project->buildDir));
    exportOptions.excludedDirectories.push_back(exportDirectory);

    std::string exportError;
    if (!serializer.writeProject(projectRoot, entryScenePath, packetPath, exportOptions, &exportError))
    {
        m_notificationManager.showError("Game export failed.");
        VX_EDITOR_ERROR_STREAM("Failed to export .elixpacket: " << exportError << '\n');
        return;
    }

    auto copyFile = [&](const std::filesystem::path &sourcePath,
                        const std::filesystem::path &targetPath,
                        const std::string &assetName) -> bool
    {
        if (sourcePath.empty() || !std::filesystem::exists(sourcePath) || !std::filesystem::is_regular_file(sourcePath))
        {
            VX_EDITOR_ERROR_STREAM("Game export failed: " << assetName << " source file is missing: " << sourcePath << '\n');
            return false;
        }

        std::error_code createParentDirectoryError;
        std::filesystem::create_directories(targetPath.parent_path(), createParentDirectoryError);
        if (createParentDirectoryError)
        {
            VX_EDITOR_ERROR_STREAM("Game export failed: cannot create target directory for " << assetName << ": " << targetPath.parent_path()
                                                                                             << " (" << createParentDirectoryError.message() << ")\n");
            return false;
        }

        std::error_code copyError;
        std::filesystem::copy_file(sourcePath, targetPath, std::filesystem::copy_options::overwrite_existing, copyError);
        if (copyError)
        {
            VX_EDITOR_ERROR_STREAM("Game export failed: cannot copy " << assetName << " from '" << sourcePath << "' to '" << targetPath
                                                                      << "' (" << copyError.message() << ")\n");
            return false;
        }

        return true;
    };

    auto copyDirectoryContents = [&](const std::filesystem::path &sourceDirectory,
                                     const std::filesystem::path &targetDirectory) -> bool
    {
        if (sourceDirectory.empty() || !std::filesystem::exists(sourceDirectory) || !std::filesystem::is_directory(sourceDirectory))
            return true;

        std::error_code createDirectoryError;
        std::filesystem::create_directories(targetDirectory, createDirectoryError);
        if (createDirectoryError)
        {
            VX_EDITOR_ERROR_STREAM("Game export failed: cannot create directory '" << targetDirectory << "': " << createDirectoryError.message() << '\n');
            return false;
        }

        std::error_code iteratorError;
        std::filesystem::recursive_directory_iterator iterator(
            sourceDirectory,
            std::filesystem::directory_options::skip_permission_denied,
            iteratorError);
        if (iteratorError)
        {
            VX_EDITOR_ERROR_STREAM("Game export failed: cannot enumerate directory '" << sourceDirectory << "': " << iteratorError.message() << '\n');
            return false;
        }

        for (auto it = iterator; it != std::filesystem::recursive_directory_iterator(); ++it)
        {
            std::error_code relativeError;
            const std::filesystem::path relativePath = std::filesystem::relative(it->path(), sourceDirectory, relativeError).lexically_normal();
            if (relativeError || relativePath.empty())
            {
                VX_EDITOR_ERROR_STREAM("Game export failed: cannot compute relative path for '" << it->path() << "'.\n");
                return false;
            }

            const std::filesystem::path targetPath = targetDirectory / relativePath;

            if (it->is_directory())
            {
                std::error_code createSubdirectoryError;
                std::filesystem::create_directories(targetPath, createSubdirectoryError);
                if (createSubdirectoryError)
                {
                    VX_EDITOR_ERROR_STREAM("Game export failed: cannot create directory '" << targetPath << "': " << createSubdirectoryError.message() << '\n');
                    return false;
                }

                continue;
            }

            if (!it->is_regular_file())
                continue;

            std::error_code createParentDirectoryError;
            std::filesystem::create_directories(targetPath.parent_path(), createParentDirectoryError);
            if (createParentDirectoryError)
            {
                VX_EDITOR_ERROR_STREAM("Game export failed: cannot create target directory '" << targetPath.parent_path()
                                                                                              << "': " << createParentDirectoryError.message() << '\n');
                return false;
            }

            std::error_code copyError;
            std::filesystem::copy_file(it->path(), targetPath, std::filesystem::copy_options::overwrite_existing, copyError);
            if (copyError)
            {
                VX_EDITOR_ERROR_STREAM("Game export failed: cannot copy file '" << it->path() << "' to '" << targetPath
                                                                                << "': " << copyError.message() << '\n');
                return false;
            }
        }

        return true;
    };

    const std::filesystem::path runningExecutablePath = FileHelper::getExecutableFilePath();
    const std::filesystem::path runtimeExecutablePath = findPreferredRuntimeExecutableForExport(runningExecutablePath);
    if (runtimeExecutablePath.empty() || !std::filesystem::exists(runtimeExecutablePath) || !std::filesystem::is_regular_file(runtimeExecutablePath))
    {
        m_notificationManager.showError("Export failed: engine executable was not found.");
        VX_EDITOR_ERROR_STREAM("Game export failed: runtime executable path is invalid: " << runtimeExecutablePath << '\n');
        return;
    }

    if (runtimeExecutablePath != runningExecutablePath)
        VX_EDITOR_INFO_STREAM("Game export will use runtime executable: " << runtimeExecutablePath << '\n');
    else if (buildArtifactPreference(runtimeExecutablePath) >= 4)
        VX_EDITOR_WARNING_STREAM("Game export is using a Debug runtime executable. Build the engine in Release to package optimized runtime binaries.\n");

    std::string packagedExecutableName = packetBaseName;
#if defined(_WIN32)
    packagedExecutableName += ".exe";
#endif
    const std::filesystem::path packagedExecutablePath = exportDirectory / packagedExecutableName;

    if (!copyFile(runtimeExecutablePath, packagedExecutablePath, "runtime executable"))
    {
        m_notificationManager.showError("Export failed while copying runtime executable.");
        return;
    }

#if !defined(_WIN32)
    std::error_code setPermissionsError;
    std::filesystem::permissions(
        packagedExecutablePath,
        std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add,
        setPermissionsError);
    if (setPermissionsError)
        VX_EDITOR_WARNING_STREAM("Failed to add executable bit for '" << packagedExecutablePath << "': " << setPermissionsError.message() << '\n');
#endif

    const std::filesystem::path packagedModulePath = exportDirectory / moduleLibraryPath.filename();
    if (!copyFile(moduleLibraryPath, packagedModulePath, "GameModule library"))
    {
        m_notificationManager.showError("Export failed while copying GameModule library.");
        return;
    }

    std::unordered_map<std::string, std::filesystem::path> preferredVelixSdkLibrariesByName;
    std::error_code iteratorError;
    std::filesystem::recursive_directory_iterator iterator(
        buildDirectory,
        std::filesystem::directory_options::skip_permission_denied,
        iteratorError);
    if (!iteratorError)
    {
        for (auto it = iterator; it != std::filesystem::recursive_directory_iterator(); ++it)
        {
            if (!it->is_regular_file())
                continue;

            const std::string fileName = it->path().filename().string();
            if (fileName.find("VelixSDK") == std::string::npos)
                continue;

#if defined(_WIN32)
            if (it->path().extension() != ".dll")
                continue;
#else
            if (fileName.find(".so") == std::string::npos)
                continue;
#endif
            auto existing = preferredVelixSdkLibrariesByName.find(fileName);
            if (existing == preferredVelixSdkLibrariesByName.end() ||
                buildArtifactPreference(it->path()) < buildArtifactPreference(existing->second))
                preferredVelixSdkLibrariesByName[fileName] = it->path();
        }
    }
    else
    {
        VX_EDITOR_WARNING_STREAM("Failed to enumerate build directory for VelixSDK runtime libraries: " << iteratorError.message() << '\n');
    }

    std::vector<std::filesystem::path> velixSdkLibraries;
    velixSdkLibraries.reserve(preferredVelixSdkLibrariesByName.size());
    for (const auto &[_, path] : preferredVelixSdkLibrariesByName)
        velixSdkLibraries.push_back(path);

    std::sort(velixSdkLibraries.begin(), velixSdkLibraries.end(), [](const auto &lhs, const auto &rhs)
              { return lhs.string() < rhs.string(); });

    for (const auto &sdkLibraryPath : velixSdkLibraries)
    {
        const std::filesystem::path targetPath = exportDirectory / sdkLibraryPath.filename();
        if (!copyFile(sdkLibraryPath, targetPath, "VelixSDK runtime library"))
        {
            m_notificationManager.showError("Export failed while copying VelixSDK runtime library.");
            return;
        }
    }

    const std::filesystem::path runtimeDirectory = runtimeExecutablePath.parent_path();

    if (!copyDirectoryContents(runtimeDirectory / "resources", exportDirectory / "resources"))
    {
        m_notificationManager.showError("Export failed while copying runtime resources.");
        return;
    }

    if (!copyDirectoryContents(runtimeDirectory / "lib", exportDirectory / "lib"))
    {
        m_notificationManager.showError("Export failed while copying runtime libraries.");
        return;
    }

    std::error_code runtimeIteratorError;
    for (const auto &entry : std::filesystem::directory_iterator(runtimeDirectory, runtimeIteratorError))
    {
        if (runtimeIteratorError)
        {
            VX_EDITOR_WARNING_STREAM("Failed to enumerate runtime directory for optional dynamic libraries: " << runtimeIteratorError.message() << '\n');
            break;
        }

        if (!entry.is_regular_file())
            continue;

        const std::string fileName = entry.path().filename().string();
        const bool isFmodRuntime =
#if defined(_WIN32)
            fileName == "fmod.dll";
#else
            fileName.rfind("libfmod.so", 0) == 0;
#endif
        if (!isFmodRuntime)
            continue;

        const std::filesystem::path targetPath = exportDirectory / entry.path().filename();
        if (!copyFile(entry.path(), targetPath, "FMOD runtime library"))
        {
            m_notificationManager.showError("Export failed while copying FMOD runtime library.");
            return;
        }
    }

    m_notificationManager.showSuccess("Game export completed: " + exportDirectory.string());
    VX_EDITOR_INFO_STREAM("Game export completed.\n"
                          << "  Packet: " << packetPath << '\n'
                          << "  Executable: " << packagedExecutablePath << '\n'
                          << "  Module: " << packagedModulePath << '\n');
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

        const bool isPlaying = m_currentMode == EditorMode::PLAY;
        const bool isPaused = m_currentMode == EditorMode::PAUSE;
        const bool isEditing = m_currentMode == EditorMode::EDIT;

        // Play / Pause button — green when in edit/pause, yellow when playing
        if (isPlaying)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.55f, 0.00f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.68f, 0.00f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.45f, 0.00f, 1.00f));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.20f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.68f, 0.25f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.42f, 0.15f, 1.00f));
        }

        const char *playText = isPlaying ? "Pause" : "Play";
        if (ImGui::Button(playText))
        {
            if (isEditing || isPaused)
            {
                if (isEditing && hasUnsavedSceneChanges())
                {
                    m_notificationManager.showWarning("Scene has unsaved changes. Save scene before Play.");
                    VX_EDITOR_WARNING_STREAM("Blocked Play due to unsaved scene changes.\n");
                }
                else
                {
                    changeMode(EditorMode::PLAY);
                }
            }
            else if (isPlaying)
            {
                changeMode(EditorMode::PAUSE);
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        // Stop button — only colored red when not already in edit mode
        if (!isEditing)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.12f, 0.10f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.18f, 0.15f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.48f, 0.08f, 0.07f, 1.00f));
        }
        if (ImGui::Button("Stop"))
            changeMode(EditorMode::EDIT);
        if (!isEditing)
            ImGui::PopStyleColor(3);

        ImGui::SameLine();

        if (ImGui::Button("Render Settings"))
            m_showRenderSettings = !m_showRenderSettings;

        ImGui::SameLine();

        if (ImGui::Button("Camera Settings"))
            m_showEditorCameraSettings = !m_showEditorCameraSettings;

        ImGui::SameLine();

        if (ImGui::Button("Benchmark"))
            m_showBenchmark = !m_showBenchmark;

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.45f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.60f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.18f, 0.35f, 1.00f));
        if (ImGui::Button("Dev Tools"))
            m_showDevTools = !m_showDevTools;
        ImGui::PopStyleColor(3);

        ImGui::EndMenuBar();
    }

    ImGui::End();
}

void Editor::drawRenderSettings()
{
    if (!m_showRenderSettings)
        return;

    ImGui::SetNextWindowSize(ImVec2(420, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(80, 80), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Render Settings", &m_showRenderSettings))
    {
        ImGui::End();
        return;
    }

    auto &settings = engine::RenderQualitySettings::getInstance();
    const size_t settingsHashBefore = hashRenderQualitySettings(settings);

    ImGui::SeparatorText("General");
    ImGui::Checkbox("VSync", &settings.enableVSync);
    ImGui::SetItemTooltip("ON = sync to monitor refresh (less tearing). OFF = uncap FPS (may tear).");
    ImGui::Checkbox("Enable Post-Processing", &settings.enablePostProcessing);

    ImGui::DragFloat("Render Scale", &settings.renderScale, 0.01f, 0.25f, 2.0f, "%.2f");
    ImGui::SetItemTooltip("1.0 = native resolution. Values below 1 reduce quality but improve performance.");

    ImGui::SeparatorText("Culling");
    ImGui::Checkbox("Small Feature Culling", &settings.enableSmallFeatureCulling);
    ImGui::SetItemTooltip("Skip meshes whose bounding sphere projects to fewer pixels than the threshold.\nReduces draw calls for dense scenes (chairs, bottles, wires, etc.).");
    if (settings.enableSmallFeatureCulling)
    {
        ImGui::DragFloat("Min Projected Radius (px)", &settings.smallFeatureCullingThreshold, 0.1f, 0.5f, 16.0f, "%.1f px");
        ImGui::SetItemTooltip("Minimum projected bounding-sphere radius in screen pixels before a mesh is culled.\n2 px = nearly invisible. Increase for more aggressive culling.");
    }

    ImGui::SeparatorText("Ray Tracing");
    const auto context = core::VulkanContext::getContext();
    const bool supportsRayQuery = context && context->hasRayQuerySupport();
    const bool supportsPipeline = context && context->hasRayTracingPipelineSupport();
    const bool supportsAnyRayTracing = supportsRayQuery || supportsPipeline;

    if (!supportsAnyRayTracing)
    {
        ImGui::TextDisabled("This GPU/runtime does not expose Vulkan ray tracing support.");
        ImGui::TextDisabled("The project settings still persist, but rendering will stay raster-only.");
    }
    else
    {
        if (supportsRayQuery && supportsPipeline)
            ImGui::TextDisabled("Available modes: Ray Query, Pipeline");
        else if (supportsPipeline)
            ImGui::TextDisabled("Available mode: Pipeline");
        else
            ImGui::TextDisabled("Available mode: Ray Query");
    }

    ImGui::BeginDisabled(!supportsAnyRayTracing);
    ImGui::Checkbox("Enable RTX", &settings.enableRayTracing);
    if (settings.enableRayTracing)
    {
        ImGui::Indent();

        if (supportsRayQuery && supportsPipeline)
        {
            const char *rtModes[] = {"Ray Query", "Pipeline"};
            int rtModeIndex = settings.rayTracingMode == engine::RenderQualitySettings::RayTracingMode::Pipeline ? 1 : 0;
            if (ImGui::Combo("Mode##rt_mode", &rtModeIndex, rtModes, IM_ARRAYSIZE(rtModes)))
            {
                settings.rayTracingMode = (rtModeIndex == 0)
                                              ? engine::RenderQualitySettings::RayTracingMode::RayQuery
                                              : engine::RenderQualitySettings::RayTracingMode::Pipeline;
            }
        }
        else if (supportsPipeline)
        {
            settings.rayTracingMode = engine::RenderQualitySettings::RayTracingMode::Pipeline;
            ImGui::TextDisabled("Mode: Pipeline");
        }
        else if (supportsRayQuery)
        {
            settings.rayTracingMode = engine::RenderQualitySettings::RayTracingMode::RayQuery;
            ImGui::TextDisabled("Mode: Ray Query");
        }

        ImGui::Checkbox("RT Shadows", &settings.enableRTShadows);
        if (settings.enableRTShadows)
        {
            ImGui::Indent();
            ImGui::SliderInt("Shadow Samples##rt", &settings.rtShadowSamples, 1, 16,
                             settings.rtShadowSamples == 1 ? "1 (hard)" : "%d");
            ImGui::SetItemTooltip("Rays per light. 1=hard shadow, 4-8=soft, 16=high quality.");
            ImGui::SliderFloat("Penumbra Size##rt", &settings.rtShadowPenumbraSize, 0.0f, 2.0f, "%.3f");
            ImGui::SetItemTooltip("Virtual light radius. Larger = wider, softer penumbra.");
            ImGui::Unindent();
        }
        ImGui::Checkbox("RT AO", &settings.enableRTAO);
        if (settings.enableRTAO)
        {
            ImGui::Indent();
            ImGui::SliderFloat("AO Radius##rtao", &settings.rtaoRadius, 0.1f, 5.0f, "%.2f");
            ImGui::SetItemTooltip("World-space occlusion search radius.");
            ImGui::SliderInt("AO Samples##rtao", &settings.rtaoSamples, 1, 16, "%d");
            ImGui::SetItemTooltip("Rays per pixel. Higher = smoother, slower.");
            ImGui::Unindent();
        }
        ImGui::Checkbox("RT Reflections", &settings.enableRTReflections);
        if (settings.enableRTReflections)
        {
            ImGui::Indent();
            ImGui::SliderInt("Reflection Samples##rt", &settings.rtReflectionSamples, 1, 8,
                             settings.rtReflectionSamples == 1 ? "1 (mirror)" : "%d");
            ImGui::SetItemTooltip("Rays per pixel. 1=perfect mirror, 4-8=glossy blur.");
            ImGui::SliderFloat("Roughness Threshold##rt", &settings.rtRoughnessThreshold, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("Skip surfaces rougher than this. Lower = only shiny metals reflect.");
            ImGui::SliderFloat("Reflection Strength##rt", &settings.rtReflectionStrength, 0.0f, 2.0f, "%.2f");
            ImGui::SetItemTooltip("Overall reflection intensity multiplier.");
            ImGui::Unindent();
        }
        ImGui::Unindent();
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Shadows");
    {
        const char *shadowItems[] = {"Low (512)", "Medium (1024)", "High (2048)", "Ultra (4096)"};
        int shadowIndex = 0;
        switch (settings.shadowQuality)
        {
        case engine::RenderQualitySettings::ShadowQuality::Low:
            shadowIndex = 0;
            break;
        case engine::RenderQualitySettings::ShadowQuality::Medium:
            shadowIndex = 1;
            break;
        case engine::RenderQualitySettings::ShadowQuality::High:
            shadowIndex = 2;
            break;
        case engine::RenderQualitySettings::ShadowQuality::Ultra:
            shadowIndex = 3;
            break;
        }

        if (ImGui::Combo("Shadow Quality", &shadowIndex, shadowItems, 4))
        {
            switch (shadowIndex)
            {
            case 0:
                settings.shadowQuality = engine::RenderQualitySettings::ShadowQuality::Low;
                break;
            case 1:
                settings.shadowQuality = engine::RenderQualitySettings::ShadowQuality::Medium;
                break;
            case 2:
                settings.shadowQuality = engine::RenderQualitySettings::ShadowQuality::High;
                break;
            case 3:
                settings.shadowQuality = engine::RenderQualitySettings::ShadowQuality::Ultra;
                break;
            }
        }

        const char *cascadeItems[] = {"1 Cascade", "2 Cascades", "4 Cascades"};
        int cascadeIndex = 2;
        switch (settings.shadowCascadeCount)
        {
        case engine::RenderQualitySettings::ShadowCascadeCount::X1:
            cascadeIndex = 0;
            break;
        case engine::RenderQualitySettings::ShadowCascadeCount::X2:
            cascadeIndex = 1;
            break;
        case engine::RenderQualitySettings::ShadowCascadeCount::X4:
        default:
            cascadeIndex = 2;
            break;
        }

        if (ImGui::Combo("Directional Cascades", &cascadeIndex, cascadeItems, IM_ARRAYSIZE(cascadeItems)))
        {
            switch (cascadeIndex)
            {
            case 0:
                settings.shadowCascadeCount = engine::RenderQualitySettings::ShadowCascadeCount::X1;
                break;
            case 1:
                settings.shadowCascadeCount = engine::RenderQualitySettings::ShadowCascadeCount::X2;
                break;
            case 2:
            default:
                settings.shadowCascadeCount = engine::RenderQualitySettings::ShadowCascadeCount::X4;
                break;
            }
        }

        ImGui::DragFloat("Shadow Max Distance", &settings.shadowMaxDistance, 1.0f, 20.0f, 2000.0f, "%.0f");
        ImGui::SetItemTooltip("Limits directional-shadow cascade range in world units. Lower values reduce shadow draw calls and CPU cost.");

        ImGui::TextDisabled("More cascades improve distance quality but increase shadow draw calls.");
    }

    ImGui::SeparatorText("Ambient Occlusion");
    ImGui::Checkbox("SSAO", &settings.enableSSAO);
    if (settings.enableSSAO)
    {
        ImGui::Indent();
        ImGui::DragFloat("Radius##ssao", &settings.ssaoRadius, 0.01f, 0.05f, 5.0f, "%.2f");
        ImGui::DragFloat("Bias##ssao", &settings.ssaoBias, 0.001f, 0.0f, 0.1f, "%.4f");
        ImGui::DragFloat("Strength##ssao", &settings.ssaoStrength, 0.05f, 0.1f, 5.0f, "%.2f");
        ImGui::DragInt("Samples##ssao", &settings.ssaoSamples, 1, 4, 64);
        ImGui::Checkbox("GTAO Mode##gtao", &settings.enableGTAO);
        if (settings.enableGTAO)
        {
            ImGui::Indent();
            ImGui::DragInt("Directions##gtao", &settings.gtaoDirections, 1, 2, 8);
            ImGui::DragInt("Steps##gtao", &settings.gtaoSteps, 1, 2, 8);
            ImGui::Checkbox("Use Bent Normals##gtao", &settings.useBentNormals);
            ImGui::Unindent();
        }
        ImGui::Unindent();
    }

    ImGui::SeparatorText("Bloom");
    ImGui::Checkbox("Bloom##toggle", &settings.enableBloom);
    if (settings.enableBloom)
    {
        ImGui::Indent();
        ImGui::DragFloat("Threshold##bloom", &settings.bloomThreshold, 0.01f, 0.0f, 5.0f, "%.2f");
        ImGui::DragFloat("Knee##bloom", &settings.bloomKnee, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Strength##bloom", &settings.bloomStrength, 0.01f, 0.0f, 2.0f, "%.2f");
        ImGui::Unindent();
    }

    ImGui::SeparatorText("Environment");
    if (m_scene && m_scene->hasSkyboxHDR())
    {
        ImGui::TextWrapped("HDR Skybox: %s", m_scene->getSkyboxHDRPath().c_str());
        if (ImGui::Button("Remove HDR Skybox##render_settings"))
        {
            m_scene->clearSkyboxHDR();
            m_notificationManager.showInfo("Removed HDR skybox from scene");
        }
    }
    else
    {
        ImGui::TextDisabled("HDR Skybox: <None>");
    }

    ImGui::SeparatorText("Ambient Shadowing");
    ImGui::DragFloat("Shadow on Ambient##ambient_shadow", &settings.shadowAmbientStrength, 0.01f, 0.0f, 1.0f, "%.2f");

    ImGui::SeparatorText("Contact Shadows");
    ImGui::Checkbox("Contact Shadows##toggle", &settings.enableContactShadows);
    if (settings.enableContactShadows)
    {
        ImGui::Indent();
        ImGui::DragFloat("Length##cs", &settings.contactShadowLength, 0.01f, 0.1f, 5.0f, "%.2f");
        ImGui::DragFloat("Strength##cs", &settings.contactShadowStrength, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragInt("Steps##cs", &settings.contactShadowSteps, 1, 4, 32);
        ImGui::Unindent();
    }

    ImGui::SeparatorText("Color Grading");
    ImGui::Checkbox("Color Grading##toggle", &settings.enableColorGrading);
    if (settings.enableColorGrading)
    {
        ImGui::Indent();
        ImGui::DragFloat("Saturation##cg", &settings.colorGradingSaturation, 0.01f, 0.0f, 2.0f, "%.2f");
        ImGui::DragFloat("Contrast##cg", &settings.colorGradingContrast, 0.01f, 0.0f, 2.0f, "%.2f");
        ImGui::DragFloat("Temperature##cg", &settings.colorGradingTemperature, 0.01f, -1.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("-1 = cool/blue, +1 = warm/orange");
        ImGui::DragFloat("Tint##cg", &settings.colorGradingTint, 0.01f, -1.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("-1 = magenta, +1 = green");
        ImGui::Unindent();
    }

    ImGui::SeparatorText("Cinematic Effects");
    ImGui::Checkbox("Vignette##toggle", &settings.enableVignette);
    if (settings.enableVignette)
    {
        ImGui::Indent();
        ImGui::DragFloat("Strength##vig", &settings.vignetteStrength, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::Unindent();
    }
    ImGui::Checkbox("Film Grain##toggle", &settings.enableFilmGrain);
    if (settings.enableFilmGrain)
    {
        ImGui::Indent();
        ImGui::DragFloat("Strength##grain", &settings.filmGrainStrength, 0.001f, 0.0f, 0.2f, "%.3f");
        ImGui::Unindent();
    }
    ImGui::Checkbox("Chromatic Aberration##toggle", &settings.enableChromaticAberration);
    if (settings.enableChromaticAberration)
    {
        ImGui::Indent();
        ImGui::DragFloat("Strength##ca", &settings.chromaticAberrationStrength, 0.0001f, 0.0f, 0.02f, "%.4f");
        ImGui::Unindent();
    }

    ImGui::SeparatorText("Anti-Aliasing");

    const char *aaModes[] = {
        "None",
        "FXAA",
        "SMAA",
        "TAA (Experimental)",
        "CMAA"};

    int aaModeIndex = static_cast<int>(settings.getAntiAliasingMode());
    if (ImGui::Combo("Mode##aa_mode", &aaModeIndex, aaModes, IM_ARRAYSIZE(aaModes)))
        settings.setAntiAliasingMode(static_cast<engine::RenderQualitySettings::AntiAliasingMode>(aaModeIndex));

    ImGui::SetItemTooltip("Choose one AA method. FXAA/SMAA/CMAA are implemented; TAA is experimental and not fully wired.");

    const auto aaMode = settings.getAntiAliasingMode();
    if (aaMode == engine::RenderQualitySettings::AntiAliasingMode::FXAA)
        ImGui::TextDisabled("FXAA: fast, cheapest, softer image.");
    else if (aaMode == engine::RenderQualitySettings::AntiAliasingMode::SMAA)
        ImGui::TextDisabled("SMAA: sharper edges than FXAA, slightly heavier.");
    else if (aaMode == engine::RenderQualitySettings::AntiAliasingMode::CMAA)
        ImGui::TextDisabled("CMAA: conservative morphological AA, keeps more fine detail than FXAA.");
    else if (aaMode == engine::RenderQualitySettings::AntiAliasingMode::TAA)
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "TAA requires velocity buffer support and is not fully wired yet.");
    else
        ImGui::TextDisabled("AA disabled.");

    const char *anisotropyModes[] = {
        "OFF",
        "2x",
        "4x",
        "8x",
        "16x"};
    int anisotropyModeIndex = static_cast<int>(settings.anisotropyMode);
    if (ImGui::Combo("Anisotropic Filtering##anisotropy_mode", &anisotropyModeIndex, anisotropyModes, IM_ARRAYSIZE(anisotropyModes)))
        settings.anisotropyMode = static_cast<engine::RenderQualitySettings::AnisotropyMode>(anisotropyModeIndex);

    ImGui::TextDisabled("Applies to texture sampling. Unsupported levels are clamped by GPU capabilities.");

    if (settingsHashBefore != hashRenderQualitySettings(settings))
        saveProjectConfig();

    ImGui::End();
}

void Editor::saveProjectConfig()
{
    auto project = m_currentProject.lock();
    if (!project)
        return;

    m_projectConfig.captureRenderSettings();

    if (m_editorCamera)
    {
        engine::ProjectConfig::CameraSettings cam{};
        cam.moveSpeed = std::max(m_movementSpeed, 0.05f);
        cam.mouseSensitivity = std::max(m_mouseSensitivity, 0.005f);
        cam.projectionMode = static_cast<uint8_t>(m_editorCamera->getProjectionMode());
        cam.nearPlane = std::max(m_editorCamera->getNear(), 0.001f);
        cam.farPlane = std::max(m_editorCamera->getFar(), cam.nearPlane + 0.001f);
        cam.fov = std::clamp(m_editorCamera->getFOV(), 1.0f, 179.0f);
        cam.orthographicSize = std::max(m_editorCamera->getOrthographicSize(), 0.01f);
        const auto pos = m_editorCamera->getPosition();
        cam.positionX = pos.x;
        cam.positionY = pos.y;
        cam.positionZ = pos.z;
        cam.yaw = m_editorCamera->getYaw();
        cam.pitch = m_editorCamera->getPitch();
        m_projectConfig.setCameraSettings(cam);
    }

    if (!m_projectConfig.save(std::filesystem::path(project->fullPath)))
        VX_EDITOR_WARNING_STREAM("Failed to save project config\n");
}

void Editor::loadProjectConfig()
{
    auto project = m_currentProject.lock();
    if (!project)
        return;

    m_projectConfig.load(std::filesystem::path(project->fullPath));
    m_projectConfig.applyRenderSettings();

    if (!m_editorCamera)
        return;

    const auto &cam = m_projectConfig.getCameraSettings();
    m_movementSpeed = std::max(cam.moveSpeed, 0.05f);
    m_mouseSensitivity = std::max(cam.mouseSensitivity, 0.005f);
    m_editorCamera->setProjectionMode(static_cast<engine::Camera::ProjectionMode>(
        std::clamp(static_cast<int>(cam.projectionMode), 0, 1)));
    m_editorCamera->setNear(std::max(cam.nearPlane, 0.001f));
    m_editorCamera->setFar(std::max(cam.farPlane, m_editorCamera->getNear() + 0.001f));
    m_editorCamera->setFOV(std::clamp(cam.fov, 1.0f, 179.0f));
    m_editorCamera->setOrthographicSize(std::max(cam.orthographicSize, 0.01f));
    m_editorCamera->setPosition({cam.positionX, cam.positionY, cam.positionZ});
    m_editorCamera->setYaw(cam.yaw);
    m_editorCamera->setPitch(cam.pitch);
}

void Editor::drawEditorCameraSettings()
{
    if (!m_showEditorCameraSettings)
        return;

    ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(520, 80), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Editor Camera Settings", &m_showEditorCameraSettings))
    {
        ImGui::End();
        return;
    }

    if (!m_editorCamera)
    {
        ImGui::TextDisabled("Editor camera is unavailable.");
        ImGui::End();
        return;
    }

    bool cameraSettingsChanged = false;

    if (ImGui::DragFloat("Move Speed", &m_movementSpeed, 0.05f, 0.05f, 200.0f, "%.2f"))
    {
        m_movementSpeed = std::max(m_movementSpeed, 0.05f);
        cameraSettingsChanged = true;
    }

    if (ImGui::DragFloat("Mouse Sensitivity", &m_mouseSensitivity, 0.001f, 0.005f, 2.0f, "%.3f"))
    {
        m_mouseSensitivity = std::max(m_mouseSensitivity, 0.005f);
        cameraSettingsChanged = true;
    }

    const char *projectionModes[] = {"Perspective", "Orthographic"};
    int projectionMode = static_cast<int>(m_editorCamera->getProjectionMode());
    if (ImGui::Combo("Projection", &projectionMode, projectionModes, IM_ARRAYSIZE(projectionModes)))
    {
        m_editorCamera->setProjectionMode(static_cast<engine::Camera::ProjectionMode>(projectionMode));
        cameraSettingsChanged = true;
    }

    float nearPlane = m_editorCamera->getNear();
    float farPlane = m_editorCamera->getFar();
    const bool nearChanged = ImGui::DragFloat("Near Plane", &nearPlane, 0.01f, 0.001f, 1000.0f, "%.3f");
    const bool farChanged = ImGui::DragFloat("Far Plane", &farPlane, 1.0f, 0.01f, 10000.0f, "%.2f");
    if (nearChanged || farChanged)
    {
        nearPlane = std::max(nearPlane, 0.001f);
        farPlane = std::max(farPlane, nearPlane + 0.001f);
        m_editorCamera->setNear(nearPlane);
        m_editorCamera->setFar(farPlane);
        cameraSettingsChanged = true;
    }

    if (m_editorCamera->getProjectionMode() == engine::Camera::ProjectionMode::Perspective)
    {
        float fov = m_editorCamera->getFOV();
        if (ImGui::DragFloat("FOV", &fov, 0.1f, 1.0f, 179.0f, "%.1f"))
        {
            m_editorCamera->setFOV(fov);
            cameraSettingsChanged = true;
        }
    }
    else
    {
        float orthographicSize = m_editorCamera->getOrthographicSize();
        if (ImGui::DragFloat("Ortho Size", &orthographicSize, 0.05f, 0.01f, 2000.0f, "%.2f"))
        {
            m_editorCamera->setOrthographicSize(orthographicSize);
            cameraSettingsChanged = true;
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Reset Editor Camera"))
    {
        m_editorCamera->setProjectionMode(engine::Camera::ProjectionMode::Perspective);
        m_editorCamera->setPosition(glm::vec3(0.0f, 1.0f, 5.0f));
        m_editorCamera->setYaw(-90.0f);
        m_editorCamera->setPitch(0.0f);
        m_editorCamera->setNear(0.1f);
        m_editorCamera->setFar(1000.0f);
        m_editorCamera->setFOV(60.0f);
        m_movementSpeed = 3.0f;
        m_mouseSensitivity = 0.1f;
        cameraSettingsChanged = true;
    }

    if (cameraSettingsChanged)
        saveProjectConfig();

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

    ImGui::SameLine();

    if (ImGui::Button("UI Tools"))
        m_showUITools = !m_showUITools;

    ImGui::SameLine();

    if (ImGui::Button("Terrain Tools"))
        m_showTerrainTools = !m_showTerrainTools;

    ImGui::End();
}

void Editor::drawFrame(VkDescriptorSet viewportDescriptorSet,
                       VkDescriptorSet gameViewportDescriptorSet,
                       bool hasGameCamera)
{
    if (m_renderOnlyViewport && viewportDescriptorSet)
    {
        drawViewport(viewportDescriptorSet);
        return;
    }

    m_assetsPreviewSystem.beginFrame();

    handleInput();

    showDockSpace();
    syncAssetsAndTerminalDocking();

    drawCustomTitleBar();
    drawToolBar();

    if (viewportDescriptorSet)
        drawViewport(viewportDescriptorSet);
    drawGameViewport(gameViewportDescriptorSet, hasGameCamera);

    drawMaterialEditors();

    drawDocument();
    drawAssets();
    drawTerminal();
    drawBottomPanel();
    drawHierarchy();
    drawDetails();
    drawUITools();
    drawTerrainTools();
    drawEditorCameraSettings();
    drawRenderSettings();
    drawBenchmark();
    drawDevTools();

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
        clicked = true;
    }
    ImGui::EndGroup();

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            const char *droppedPath = static_cast<const char *>(payload->Data);
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
        const std::string normalizedMatPath = resolveMaterialPathAgainstProjectRoot(matPath, projectRoot);

        std::string title = matEditor.path.filename().string();
        if (matEditor.dirty)
            title += "*";

        std::string windowName = title + "###MaterialEditorMain";

        if (m_centerDockId != 0)
            ImGui::SetNextWindowDockID(m_centerDockId, ImGuiCond_FirstUseEver);

        bool keepOpen = matEditor.open;
        bool closeRequested = false;

        auto destroyNodeEditorState = [this, &normalizedMatPath]()
        {
            auto stateIt = m_materialEditorUiState.find(normalizedMatPath);
            if (stateIt == m_materialEditorUiState.end())
                return;

            if (stateIt->second.nodeEditorContext)
            {
                ed::DestroyEditor(stateIt->second.nodeEditorContext);
                stateIt->second.nodeEditorContext = nullptr;
            }

            m_materialEditorUiState.erase(stateIt);
        };

        if (ImGui::Begin(windowName.c_str(), &keepOpen))
        {
            auto materialRecordIt = project->cache.materialsByPath.find(normalizedMatPath);
            if (materialRecordIt == project->cache.materialsByPath.end())
            {
                bool loaded = reloadMaterialFromDisk(matEditor.path);
                if (!loaded)
                {
                    std::error_code fileError;
                    const bool materialFileExists = std::filesystem::exists(matEditor.path, fileError) && !fileError;
                    bool materialFileEmpty = false;

                    if (materialFileExists)
                    {
                        fileError.clear();
                        if (std::filesystem::is_regular_file(matEditor.path, fileError) && !fileError)
                        {
                            fileError.clear();
                            const auto fileSize = std::filesystem::file_size(matEditor.path, fileError);
                            materialFileEmpty = !fileError && fileSize == 0;
                        }
                    }

                    if (!materialFileExists || materialFileEmpty)
                    {
                        engine::CPUMaterial defaultMaterial{};
                        defaultMaterial.name = matEditor.path.stem().string();

                        if (saveMaterialToDisk(matEditor.path, defaultMaterial))
                            loaded = reloadMaterialFromDisk(matEditor.path);
                    }
                }

                materialRecordIt = project->cache.materialsByPath.find(normalizedMatPath);
                if (!loaded || materialRecordIt == project->cache.materialsByPath.end())
                {
                    ImGui::TextDisabled("Failed to load material.");
                    ImGui::TextWrapped("%s", matEditor.path.string().c_str());
                    ImGui::Spacing();

                    if (ImGui::Button("Initialize Default Material"))
                    {
                        engine::CPUMaterial defaultMaterial{};
                        defaultMaterial.name = matEditor.path.stem().string();

                        if (saveMaterialToDisk(matEditor.path, defaultMaterial) && reloadMaterialFromDisk(matEditor.path))
                            m_notificationManager.showSuccess("Material initialized");
                        else
                            m_notificationManager.showError("Failed to initialize material");
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Open As Text"))
                        openTextDocument(matEditor.path);

                    ImGui::End();
                    matEditor.open = keepOpen;
                    closeRequested = !matEditor.open;
                    if (!closeRequested)
                    {
                        ++it;
                        continue;
                    }

                    destroyNodeEditorState();
                    it = m_openMaterialEditors.erase(it);
                    continue;
                }
            }
            if (materialRecordIt != project->cache.materialsByPath.end())
            {
                auto &ui = m_materialEditorUiState[normalizedMatPath];
                auto &materialAsset = materialRecordIt->second;
                auto gpuMat = materialAsset.gpu;
                auto &cpuMat = materialAsset.cpuData;

                if (!gpuMat)
                {
                    ImGui::TextDisabled("Material GPU instance is unavailable.");
                    ImGui::End();
                    matEditor.open = keepOpen;
                    closeRequested = !matEditor.open;
                    if (!closeRequested)
                    {
                        ++it;
                        continue;
                    }
                }
                else
                {
                    if (!ui.nodeEditorInitialized)
                    {
                        ed::Config config;
                        config.SettingsFile = nullptr;
                        ui.nodeEditorContext = ed::CreateEditor(&config);
                        ui.nodeEditorInitialized = true;

                        ui.linkMappingActive = true;
                        ui.linkAlbedoActive = !cpuMat.albedoTexture.empty();
                        ui.linkNormalActive = !cpuMat.normalTexture.empty();
                        ui.linkOrmActive = !cpuMat.ormTexture.empty();
                        ui.linkEmissiveActive = !cpuMat.emissiveTexture.empty();
                        ui.linkOutputActive = true;
                        ui.linkColorToEmissiveActive = false;

                        const float emissiveMaxChannel = std::max({cpuMat.emissiveFactor.r, cpuMat.emissiveFactor.g, cpuMat.emissiveFactor.b});
                        if (emissiveMaxChannel > 0.0001f)
                        {
                            ui.colorNodeValue = cpuMat.emissiveFactor / emissiveMaxChannel;
                            ui.colorNodeStrength = emissiveMaxChannel;
                            if (cpuMat.emissiveTexture.empty())
                                ui.linkColorToEmissiveActive = true;
                        }
                        else
                        {
                            ui.colorNodeValue = glm::vec3(1.0f, 1.0f, 1.0f);
                            ui.colorNodeStrength = 1.0f;
                        }

                        if (ui.nodeEditorContext)
                        {
                            ed::SetCurrentEditor(ui.nodeEditorContext);
                            ed::SetNodePosition(ed::NodeId(ui.mappingNodeId), ImVec2(20.0f, 130.0f));
                            ed::SetNodePosition(ed::NodeId(ui.texturesNodeId), ImVec2(420.0f, 70.0f));
                            ed::SetNodePosition(ed::NodeId(ui.colorNodeId), ImVec2(760.0f, 330.0f));
                            ed::SetNodePosition(ed::NodeId(ui.principledNodeId), ImVec2(920.0f, 90.0f));
                            ed::SetNodePosition(ed::NodeId(ui.outputNodeId), ImVec2(1320.0f, 120.0f));
                            ed::SetCurrentEditor(nullptr);
                        }
                    }

                    const bool isMaterialEditorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                    const bool isCtrlDown = ImGui::GetIO().KeyCtrl;

                    auto setTexturePathAndGpu = [&](std::string &cpuPath,
                                                    TextureUsage usage,
                                                    const std::string &newPath,
                                                    const std::function<void(engine::Texture::SharedPtr)> &assignTexture,
                                                    bool &linkState)
                    {
                        cpuPath = newPath;
                        if (newPath.empty())
                        {
                            assignTexture(nullptr);
                            linkState = false;
                        }
                        else
                        {
                            assignTexture(ensureProjectTextureLoaded(newPath, usage));
                            linkState = true;
                        }
                    };

                    auto saveCurrentMaterial = [&]()
                    {
                        if (saveMaterialToDisk(matEditor.path, cpuMat))
                        {
                            matEditor.dirty = false;
                            m_notificationManager.showSuccess("Material saved");
                            VX_EDITOR_INFO_STREAM("Material saved: " << normalizedMatPath);
                        }
                        else
                        {
                            m_notificationManager.showError("Failed to save material");
                            VX_EDITOR_ERROR_STREAM("Failed to save material: " << normalizedMatPath);
                        }
                    };

                    if (ImGui::Button("Save"))
                        saveCurrentMaterial();
                    ImGui::SameLine();

                    if (ImGui::Button("Revert"))
                    {
                        if (reloadMaterialFromDisk(matEditor.path))
                        {
                            auto refreshedIt = project->cache.materialsByPath.find(normalizedMatPath);
                            if (refreshedIt != project->cache.materialsByPath.end())
                            {
                                gpuMat = refreshedIt->second.gpu;
                                cpuMat = refreshedIt->second.cpuData;
                                ui.linkMappingActive = true;
                                ui.linkAlbedoActive = !cpuMat.albedoTexture.empty();
                                ui.linkNormalActive = !cpuMat.normalTexture.empty();
                                ui.linkOrmActive = !cpuMat.ormTexture.empty();
                                ui.linkEmissiveActive = !cpuMat.emissiveTexture.empty();
                                ui.linkOutputActive = true;
                                ui.linkColorToEmissiveActive = false;

                                const float emissiveMaxChannel = std::max({cpuMat.emissiveFactor.r, cpuMat.emissiveFactor.g, cpuMat.emissiveFactor.b});
                                if (emissiveMaxChannel > 0.0001f)
                                {
                                    ui.colorNodeValue = cpuMat.emissiveFactor / emissiveMaxChannel;
                                    ui.colorNodeStrength = emissiveMaxChannel;
                                    if (cpuMat.emissiveTexture.empty())
                                        ui.linkColorToEmissiveActive = true;
                                }
                                else
                                {
                                    ui.colorNodeValue = glm::vec3(1.0f, 1.0f, 1.0f);
                                    ui.colorNodeStrength = 1.0f;
                                }
                            }

                            matEditor.dirty = false;
                            m_notificationManager.showInfo("Material reloaded from disk");
                            VX_EDITOR_INFO_STREAM("Material reloaded from disk: " << normalizedMatPath);
                        }
                        else
                        {
                            m_notificationManager.showError("Failed to reload material from disk");
                            VX_EDITOR_ERROR_STREAM("Failed to reload material from disk: " << normalizedMatPath);
                        }
                    }

                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", matPath.c_str());

                    if (isMaterialEditorFocused && isCtrlDown && ImGui::IsKeyPressed(ImGuiKey_S, false))
                        saveCurrentMaterial();

                    ImGui::Separator();

                    // ── Custom Shader Expression ──────────────────────────────────────────
                    if (!ui.customShaderInitialized)
                    {
                        const auto &glslLang = TextEditor::LanguageDefinition::GLSL();
                        ui.customFunctionsEditor.SetLanguageDefinition(glslLang);
                        ui.customExpressionEditor.SetLanguageDefinition(glslLang);
                        ui.customFunctionsEditor.SetShowWhitespaces(false);
                        ui.customExpressionEditor.SetShowWhitespaces(false);

                        std::string initFuncs;
                        std::string initExpr;
                        if (!cpuMat.customExpression.empty())
                        {
                            const std::string funcMarker = "// [FUNCTIONS]\n";
                            const std::string exprMarker = "// [EXPRESSION]\n";
                            const std::string &src = cpuMat.customExpression;
                            const size_t fPos = src.find(funcMarker);
                            const size_t ePos = src.find(exprMarker);
                            if (fPos != std::string::npos && ePos != std::string::npos)
                            {
                                initFuncs = src.substr(fPos + funcMarker.size(), ePos - fPos - funcMarker.size());
                                initExpr  = src.substr(ePos + exprMarker.size());
                            }
                            else
                            {
                                initExpr = src;
                            }
                        }
                        ui.customFunctionsEditor.SetText(initFuncs);
                        ui.customExpressionEditor.SetText(initExpr);
                        ui.customShaderInitialized = true;
                    }

                    if (ImGui::CollapsingHeader("Custom Shader Expression"))
                    {
                        ui.customShaderPanelOpen = true;
                        ImGui::TextDisabled("Write GLSL to override: albedo, roughness, metallic, ao, emissive, N, alpha");
                        ImGui::TextDisabled("Read-only: uv, pc.time, fragPositionView, fragNormalView, material.*");
                        ImGui::Spacing();

                        ImGui::Text("Helper Functions (optional):");
                        ui.customFunctionsEditor.Render("##customFunctionsEditor",
                            ImVec2(-1.0f, ImGui::GetTextLineHeight() * 7.0f), true);

                        ImGui::Spacing();
                        ImGui::Text("Expression:");
                        ui.customExpressionEditor.Render("##customExpressionEditor",
                            ImVec2(-1.0f, ImGui::GetTextLineHeight() * 12.0f), true);

                        ImGui::Spacing();
                        if (ImGui::Button("Compile & Apply"))
                        {
                            std::ifstream templateFile("./resources/shaders/gbuffer_static_template.frag_template");
                            if (!templateFile.is_open())
                            {
                                ui.customShaderLastError = "Template not found: resources/shaders/gbuffer_static_template.frag_template";
                                ui.customShaderHasError = true;
                            }
                            else
                            {
                                std::string templateSrc((std::istreambuf_iterator<char>(templateFile)),
                                                         std::istreambuf_iterator<char>());

                                const std::string functions  = ui.customFunctionsEditor.GetText();
                                const std::string expression = ui.customExpressionEditor.GetText();

                                auto replaceFirst = [](std::string &s, const std::string &from, const std::string &to)
                                {
                                    const size_t pos = s.find(from);
                                    if (pos != std::string::npos)
                                        s.replace(pos, from.size(), to);
                                };

                                replaceFirst(templateSrc, "// <<ELIX_CUSTOM_FUNCTIONS>>", functions);
                                replaceFirst(templateSrc, "// <<ELIX_CUSTOM_EXPRESSION>>", expression);

                                std::vector<uint32_t> spv;
                                try
                                {
                                    spv = engine::shaders::ShaderCompiler::compileGLSL(
                                        templateSrc, shaderc_glsl_fragment_shader, 0, "custom_material.frag");
                                    ui.customShaderHasError = false;
                                    ui.customShaderLastError.clear();
                                }
                                catch (const std::exception &e)
                                {
                                    ui.customShaderLastError = e.what();
                                    ui.customShaderHasError = true;
                                }

                                if (!ui.customShaderHasError && !spv.empty())
                                {
                                    // FNV-1a hash of expression content (stable, no extra lib)
                                    const std::string hashInput = functions + "\n" + expression;
                                    uint64_t hash = 14695981039346656037ULL;
                                    for (unsigned char c : hashInput) { hash ^= c; hash *= 1099511628211ULL; }
                                    char hashStr[17];
                                    snprintf(hashStr, sizeof(hashStr), "%016llx", static_cast<unsigned long long>(hash));

                                    std::filesystem::create_directories("./resources/shaders/material_cache");
                                    const std::string spvPath = std::string("./resources/shaders/material_cache/") + hashStr + ".spv";

                                    std::ofstream spvFile(spvPath, std::ios::binary);
                                    if (!spvFile.is_open())
                                    {
                                        ui.customShaderLastError = "Failed to write SPV: " + spvPath;
                                        ui.customShaderHasError = true;
                                    }
                                    else
                                    {
                                        spvFile.write(reinterpret_cast<const char *>(spv.data()), spv.size() * sizeof(uint32_t));
                                        spvFile.close();

                                        cpuMat.customExpression = "// [FUNCTIONS]\n" + functions + "// [EXPRESSION]\n" + expression;
                                        cpuMat.customShaderHash = hashStr;
                                        gpuMat->setCustomFragPath(spvPath);

                                        matEditor.dirty = true;
                                        saveCurrentMaterial();
                                        m_notificationManager.showSuccess("Custom shader compiled and applied");
                                    }
                                }
                            }
                        }

                        if (!cpuMat.customShaderHash.empty())
                        {
                            ImGui::SameLine();
                            if (ImGui::Button("Clear"))
                            {
                                cpuMat.customExpression.clear();
                                cpuMat.customShaderHash.clear();
                                gpuMat->setCustomFragPath("");
                                ui.customFunctionsEditor.SetText("");
                                ui.customExpressionEditor.SetText("");
                                ui.customShaderLastError.clear();
                                ui.customShaderHasError = false;
                                matEditor.dirty = true;
                            }
                            ImGui::SameLine();
                            ImGui::TextDisabled("hash: %s", cpuMat.customShaderHash.c_str());
                        }

                        if (ui.customShaderHasError)
                        {
                            ImGui::Spacing();
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                            ImGui::TextWrapped("%s", ui.customShaderLastError.c_str());
                            ImGui::PopStyleColor();
                        }
                    }
                    else
                    {
                        ui.customShaderPanelOpen = false;
                    }

                    ImGui::Separator();

                    const float minGraphHeight = 520.0f;
                    const ImVec2 availableRegion = ImGui::GetContentRegionAvail();
                    const float graphHeight = std::max(minGraphHeight, availableRegion.y - 10.0f);

                    if (!ui.nodeEditorContext)
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Node editor context creation failed.");
                        ImGui::TextDisabled("Check imgui-node-editor integration and rebuild.");
                        ImGui::End();
                        matEditor.open = keepOpen;
                        closeRequested = !matEditor.open;
                        if (!closeRequested)
                        {
                            ++it;
                            continue;
                        }
                    }

                    if (ui.nodeEditorContext)
                        ed::SetCurrentEditor(ui.nodeEditorContext);

                    ed::Begin(("MaterialGraph##" + normalizedMatPath).c_str(), ImVec2(0.0f, graphHeight));

                    struct TextureSlotRow
                    {
                        const char *label;
                        TextureUsage usage;
                        std::string *cpuPath;
                        int textureOutputPinId;
                        int principledInputPinId;
                        int linkId;
                        bool *linkState;
                        std::function<void(engine::Texture::SharedPtr)> assignTexture;
                    };

                    TextureSlotRow textureRows[] = {
                        {"Base Color", TextureUsage::Color, &cpuMat.albedoTexture, ui.texturesOutAlbedoPinId, ui.principledInAlbedoPinId, ui.linkAlbedoId, &ui.linkAlbedoActive, [&](engine::Texture::SharedPtr texture)
                         { gpuMat->setAlbedoTexture(texture); }},
                        {"Normal", TextureUsage::Data, &cpuMat.normalTexture, ui.texturesOutNormalPinId, ui.principledInNormalPinId, ui.linkNormalId, &ui.linkNormalActive, [&](engine::Texture::SharedPtr texture)
                         { gpuMat->setNormalTexture(texture); }},
                        {"Roughness/AO (ORM)", TextureUsage::Data, &cpuMat.ormTexture, ui.texturesOutOrmPinId, ui.principledInOrmPinId, ui.linkOrmId, &ui.linkOrmActive, [&](engine::Texture::SharedPtr texture)
                         { gpuMat->setOrmTexture(texture); }},
                        {"Emissive", TextureUsage::Color, &cpuMat.emissiveTexture, ui.texturesOutEmissivePinId, ui.principledInEmissivePinId, ui.linkEmissiveId, &ui.linkEmissiveActive, [&](engine::Texture::SharedPtr texture)
                         { gpuMat->setEmissiveTexture(texture); }},
                    };
                    auto disableAllColorEmissionLinks = [&]()
                    {
                        ui.linkColorToEmissiveActive = false;
                        for (auto &dynamicColorNode : ui.dynamicColorNodes)
                            dynamicColorNode.linkToEmissiveActive = false;
                    };
                    auto applyTextureSlotChange = [&](TextureSlotRow &row, const std::string &newPath)
                    {
                        setTexturePathAndGpu(*row.cpuPath, row.usage, newPath, row.assignTexture, *row.linkState);
                        if (row.principledInputPinId == ui.principledInEmissivePinId && !newPath.empty())
                            disableAllColorEmissionLinks();
                    };
                    struct DeferredTexturePopup
                    {
                        TextureSlotRow *row{nullptr};
                        std::string popupName;
                    };
                    std::vector<DeferredTexturePopup> deferredTexturePopups;
                    deferredTexturePopups.reserve(std::size(textureRows));

                    ed::BeginNode(ed::NodeId(ui.mappingNodeId));
                    ImGui::TextUnformatted("Mapping");
                    ImGui::Separator();
                    auto p = gpuMat->params();
                    glm::vec2 uvScale = {p.uvTransform.x, p.uvTransform.y};
                    glm::vec2 uvLocation = {p.uvTransform.z, p.uvTransform.w};
                    float uvRotation = p.uvRotation;

                    if (ImGui::DragFloat2("Scale", glm::value_ptr(uvScale), 0.01f, -100.0f, 100.0f))
                    {
                        gpuMat->setUVScale(uvScale);
                        cpuMat.uvScale = uvScale;
                        matEditor.dirty = true;
                    }

                    if (ImGui::DragFloat2("Location", glm::value_ptr(uvLocation), 0.01f, -100.0f, 100.0f))
                    {
                        gpuMat->setUVOffset(uvLocation);
                        cpuMat.uvOffset = uvLocation;
                        matEditor.dirty = true;
                    }

                    if (ImGui::DragFloat("Rotation (deg)", &uvRotation, 0.1f, -3600.0f, 3600.0f))
                    {
                        gpuMat->setUVRotation(uvRotation);
                        cpuMat.uvRotation = uvRotation;
                        matEditor.dirty = true;
                    }

                    float alphaCutoff = p.alphaCutoff;
                    if (ImGui::SliderFloat("Alpha Cutoff", &alphaCutoff, 0.0f, 1.0f))
                    {
                        gpuMat->setAlphaCutoff(alphaCutoff);
                        cpuMat.alphaCutoff = alphaCutoff;
                        matEditor.dirty = true;
                    }

                    ed::BeginPin(ed::PinId(ui.mappingOutVectorPinId), ed::PinKind::Output);
                    ImGui::TextUnformatted("Vector");
                    ed::EndPin();
                    ed::EndNode();

                    ed::BeginNode(ed::NodeId(ui.texturesNodeId));
                    ImGui::TextUnformatted("Textures");
                    ImGui::Separator();

                    ed::BeginPin(ed::PinId(ui.texturesInVectorPinId), ed::PinKind::Input);
                    ImGui::TextUnformatted("Vector");
                    ed::EndPin();
                    ImGui::Separator();

                    for (auto &row : textureRows)
                    {
                        ImGui::PushID(row.label);

                        ed::BeginPin(ed::PinId(row.textureOutputPinId), ed::PinKind::Output);
                        ImGui::TextUnformatted(row.label);
                        ed::EndPin();

                        ImGui::SameLine();
                        const auto texture = ensureProjectTextureLoadedPreview(*row.cpuPath, row.usage);
                        const auto descriptorSet = m_assetsPreviewSystem.getOrRequestTexturePreview(*row.cpuPath, texture);
                        const ImTextureID textureId = (ImTextureID)(uintptr_t)descriptorSet;

                        if (ImGui::ImageButton("##thumb", textureId, ImVec2(44.0f, 44.0f)))
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
                                    applyTextureSlotChange(row, textureReferencePath);
                                    matEditor.dirty = true;
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        ImGui::SameLine();
                        ImGui::BeginGroup();
                        ImGui::TextWrapped("%s", makeTextureAssetDisplayName(*row.cpuPath).c_str());
                        if (ImGui::Button("Default"))
                        {
                            applyTextureSlotChange(row, "");
                            matEditor.dirty = true;
                        }
                        ImGui::EndGroup();

                        std::string popupName = std::string("TexturePicker##") + normalizedMatPath + "_" + row.label;
                        deferredTexturePopups.push_back(DeferredTexturePopup{&row, std::move(popupName)});

                        ImGui::Separator();
                        ImGui::PopID();
                    }
                    ed::EndNode();

                    ed::BeginNode(ed::NodeId(ui.colorNodeId));
                    ImGui::TextUnformatted("Color");
                    ImGui::Separator();

                    if (ImGui::ColorEdit3("Color##ColorNode",
                                          glm::value_ptr(ui.colorNodeValue),
                                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                    {
                        if (ui.linkColorToEmissiveActive)
                        {
                            const glm::vec3 emissiveValue = ui.colorNodeValue * ui.colorNodeStrength;
                            gpuMat->setEmissiveFactor(emissiveValue);
                            cpuMat.emissiveFactor = emissiveValue;
                            matEditor.dirty = true;
                        }
                    }

                    if (ImGui::DragFloat("Strength##ColorNode", &ui.colorNodeStrength, 0.05f, 0.0f, 64.0f, "%.3f"))
                    {
                        ui.colorNodeStrength = std::max(0.0f, ui.colorNodeStrength);
                        if (ui.linkColorToEmissiveActive)
                        {
                            const glm::vec3 emissiveValue = ui.colorNodeValue * ui.colorNodeStrength;
                            gpuMat->setEmissiveFactor(emissiveValue);
                            cpuMat.emissiveFactor = emissiveValue;
                            matEditor.dirty = true;
                        }
                    }

                    ed::BeginPin(ed::PinId(ui.colorOutPinId), ed::PinKind::Output);
                    ImGui::TextUnformatted("Color");
                    ed::EndPin();
                    ed::EndNode();

                    for (auto &dynamicColorNode : ui.dynamicColorNodes)
                    {
                        if (dynamicColorNode.pendingPlacement)
                        {
                            ed::SetNodePosition(ed::NodeId(dynamicColorNode.nodeId),
                                                ImVec2(dynamicColorNode.spawnPosition.x, dynamicColorNode.spawnPosition.y));
                            dynamicColorNode.pendingPlacement = false;
                        }

                        ImGui::PushID(dynamicColorNode.nodeId);
                        ed::BeginNode(ed::NodeId(dynamicColorNode.nodeId));
                        ImGui::TextUnformatted("Color");
                        ImGui::Separator();

                        if (ImGui::ColorEdit3("Color",
                                              glm::value_ptr(dynamicColorNode.color),
                                              ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                        {
                            if (dynamicColorNode.linkToEmissiveActive)
                            {
                                const glm::vec3 emissiveValue = dynamicColorNode.color * dynamicColorNode.strength;
                                gpuMat->setEmissiveFactor(emissiveValue);
                                cpuMat.emissiveFactor = emissiveValue;
                                matEditor.dirty = true;
                            }
                        }

                        if (ImGui::DragFloat("Strength", &dynamicColorNode.strength, 0.05f, 0.0f, 64.0f, "%.3f"))
                        {
                            dynamicColorNode.strength = std::max(0.0f, dynamicColorNode.strength);
                            if (dynamicColorNode.linkToEmissiveActive)
                            {
                                const glm::vec3 emissiveValue = dynamicColorNode.color * dynamicColorNode.strength;
                                gpuMat->setEmissiveFactor(emissiveValue);
                                cpuMat.emissiveFactor = emissiveValue;
                                matEditor.dirty = true;
                            }
                        }

                        if (ImGui::SmallButton("Delete Node"))
                            dynamicColorNode.removeRequested = true;

                        ed::BeginPin(ed::PinId(dynamicColorNode.outputPinId), ed::PinKind::Output);
                        ImGui::TextUnformatted("Color");
                        ed::EndPin();
                        ed::EndNode();
                        ImGui::PopID();
                    }

                    for (size_t dynamicNodeIndex = 0; dynamicNodeIndex < ui.dynamicColorNodes.size();)
                    {
                        const bool removeNode = ui.dynamicColorNodes[dynamicNodeIndex].removeRequested;
                        if (!removeNode)
                        {
                            ++dynamicNodeIndex;
                            continue;
                        }

                        ui.dynamicColorNodes.erase(ui.dynamicColorNodes.begin() + dynamicNodeIndex);
                        matEditor.dirty = true;
                    }

                    ed::BeginNode(ed::NodeId(ui.principledNodeId));
                    ImGui::TextUnformatted("Principled BSDF");
                    ImGui::Separator();

                    auto drawInputPin = [](int pinId, const char *label)
                    {
                        ed::BeginPin(ed::PinId(pinId), ed::PinKind::Input);
                        ImGui::TextUnformatted(label);
                        ed::EndPin();
                    };

                    drawInputPin(ui.principledInAlbedoPinId, "Base Color");
                    drawInputPin(ui.principledInNormalPinId, "Normal");
                    drawInputPin(ui.principledInOrmPinId, "Roughness/AO");
                    drawInputPin(ui.principledInEmissivePinId, "Emission");
                    ImGui::Separator();

                    p = gpuMat->params();

                    glm::vec4 baseColor = p.baseColorFactor;
                    if (ImGui::ColorEdit4("Base Color",
                                          glm::value_ptr(baseColor),
                                          ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_AlphaBar))
                    {
                        gpuMat->setBaseColorFactor(baseColor);
                        cpuMat.baseColorFactor = baseColor;
                        matEditor.dirty = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Pick##BaseColor"))
                    {
                        ui.openColorPopup = true;
                        ui.colorPopupSlot = 1;
                    }

                    glm::vec3 emissiveColor = glm::vec3(p.emissiveFactor);
                    bool emissiveDrivenByColorNode = ui.linkColorToEmissiveActive;
                    for (const auto &dynamicColorNode : ui.dynamicColorNodes)
                    {
                        if (dynamicColorNode.linkToEmissiveActive)
                        {
                            emissiveDrivenByColorNode = true;
                            break;
                        }
                    }

                    if (emissiveDrivenByColorNode)
                        ImGui::BeginDisabled();
                    if (ImGui::ColorEdit3("Emission",
                                          glm::value_ptr(emissiveColor),
                                          ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                    {
                        gpuMat->setEmissiveFactor(emissiveColor);
                        cpuMat.emissiveFactor = emissiveColor;
                        matEditor.dirty = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Pick##EmissiveColor"))
                    {
                        ui.openColorPopup = true;
                        ui.colorPopupSlot = 2;
                    }
                    if (emissiveDrivenByColorNode)
                    {
                        ImGui::EndDisabled();
                        ImGui::TextDisabled("Emission controlled by Color node link");
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
                    if (ImGui::SliderFloat("Normal Scale", &normalScale, 0.0f, 4.0f))
                    {
                        gpuMat->setNormalScale(normalScale);
                        cpuMat.normalScale = normalScale;
                        matEditor.dirty = true;
                    }

                    float ior = p.ior;
                    if (ImGui::SliderFloat("IOR", &ior, 1.0f, 2.6f))
                    {
                        gpuMat->setIor(ior);
                        cpuMat.ior = ior;
                        matEditor.dirty = true;
                    }

                    uint32_t materialFlags = p.flags;
                    int shadingModeIndex = 0;
                    if ((materialFlags & engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK) != 0u)
                        shadingModeIndex = 1;
                    else if ((materialFlags & engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND) != 0u)
                        shadingModeIndex = 2;

                    ImGui::TextUnformatted("Shading");
                    bool shadingChanged = false;
                    if (ImGui::RadioButton("Opaque##material_shading", shadingModeIndex == 0))
                    {
                        shadingModeIndex = 0;
                        shadingChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Masked##material_shading", shadingModeIndex == 1))
                    {
                        shadingModeIndex = 1;
                        shadingChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Translucent##material_shading", shadingModeIndex == 2))
                    {
                        shadingModeIndex = 2;
                        shadingChanged = true;
                    }

                    if (shadingChanged)
                    {
                        materialFlags &= ~(engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK |
                                           engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND);
                        if (shadingModeIndex == 1)
                            materialFlags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK;
                        else if (shadingModeIndex == 2)
                            materialFlags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND;

                        gpuMat->setFlags(materialFlags);
                        cpuMat.flags = materialFlags;
                        matEditor.dirty = true;
                    }

                    bool twoSided = (materialFlags & engine::Material::MaterialFlags::EMATERIAL_FLAG_DOUBLE_SIDED) != 0u;
                    if (ImGui::Checkbox("Two Sided", &twoSided))
                    {
                        if (twoSided)
                            materialFlags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_DOUBLE_SIDED;
                        else
                            materialFlags &= ~engine::Material::MaterialFlags::EMATERIAL_FLAG_DOUBLE_SIDED;

                        gpuMat->setFlags(materialFlags);
                        cpuMat.flags = materialFlags;
                        matEditor.dirty = true;
                    }

                    bool flipV = (materialFlags & engine::Material::MaterialFlags::EMATERIAL_FLAG_FLIP_V) != 0u;
                    if (ImGui::Checkbox("Flip V (Blender)", &flipV))
                    {
                        if (flipV)
                            materialFlags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_FLIP_V;
                        else
                            materialFlags &= ~engine::Material::MaterialFlags::EMATERIAL_FLAG_FLIP_V;

                        gpuMat->setFlags(materialFlags);
                        cpuMat.flags = materialFlags;
                        matEditor.dirty = true;
                    }

                    bool flipU = (materialFlags & engine::Material::MaterialFlags::EMATERIAL_FLAG_FLIP_U) != 0u;
                    if (ImGui::Checkbox("Flip U (X)", &flipU))
                    {
                        if (flipU)
                            materialFlags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_FLIP_U;
                        else
                            materialFlags &= ~engine::Material::MaterialFlags::EMATERIAL_FLAG_FLIP_U;

                        gpuMat->setFlags(materialFlags);
                        cpuMat.flags = materialFlags;
                        matEditor.dirty = true;
                    }

                    bool repeatUV = (materialFlags & engine::Material::MaterialFlags::EMATERIAL_FLAG_CLAMP_UV) == 0u;
                    if (ImGui::Checkbox("Repeat UV", &repeatUV))
                    {
                        if (repeatUV)
                            materialFlags &= ~engine::Material::MaterialFlags::EMATERIAL_FLAG_CLAMP_UV;
                        else
                            materialFlags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_CLAMP_UV;

                        gpuMat->setFlags(materialFlags);
                        cpuMat.flags = materialFlags;
                        matEditor.dirty = true;
                    }

                    ed::BeginPin(ed::PinId(ui.principledOutBsdfPinId), ed::PinKind::Output);
                    ImGui::TextUnformatted("BSDF");
                    ed::EndPin();
                    ed::EndNode();

                    ed::BeginNode(ed::NodeId(ui.outputNodeId));
                    ImGui::TextUnformatted("Material Output");
                    ImGui::Separator();
                    ed::BeginPin(ed::PinId(ui.outputInSurfacePinId), ed::PinKind::Input);
                    ImGui::TextUnformatted("Surface");
                    ed::EndPin();
                    ImGui::TextDisabled("Displacement: use normal map (runtime)");
                    ed::EndNode();

                    auto drawTextureLink = [&](bool active, int linkId, int outPinId, int inPinId)
                    {
                        if (!active)
                            return;

                        ed::Link(ed::LinkId(linkId), ed::PinId(outPinId), ed::PinId(inPinId), ImColor(130, 180, 255), 2.0f);
                    };

                    drawTextureLink(ui.linkMappingActive, ui.linkMappingId, ui.mappingOutVectorPinId, ui.texturesInVectorPinId);
                    drawTextureLink(ui.linkAlbedoActive, ui.linkAlbedoId, ui.texturesOutAlbedoPinId, ui.principledInAlbedoPinId);
                    drawTextureLink(ui.linkNormalActive, ui.linkNormalId, ui.texturesOutNormalPinId, ui.principledInNormalPinId);
                    drawTextureLink(ui.linkOrmActive, ui.linkOrmId, ui.texturesOutOrmPinId, ui.principledInOrmPinId);
                    drawTextureLink(ui.linkEmissiveActive, ui.linkEmissiveId, ui.texturesOutEmissivePinId, ui.principledInEmissivePinId);
                    drawTextureLink(ui.linkColorToEmissiveActive, ui.linkColorToEmissiveId, ui.colorOutPinId, ui.principledInEmissivePinId);
                    for (const auto &dynamicColorNode : ui.dynamicColorNodes)
                        drawTextureLink(dynamicColorNode.linkToEmissiveActive, dynamicColorNode.linkId, dynamicColorNode.outputPinId, ui.principledInEmissivePinId);
                    drawTextureLink(ui.linkOutputActive, ui.linkOutputId, ui.principledOutBsdfPinId, ui.outputInSurfacePinId);

                    const bool isCreateActive = ed::BeginCreate();
                    if (isCreateActive)
                    {
                        ed::PinId startPinId;
                        ed::PinId endPinId;
                        if (ed::QueryNewLink(&startPinId, &endPinId))
                        {
                            auto acceptLinkState = [&](int fromPinId, int toPinId, bool &state, bool markDirty) -> bool
                            {
                                const bool validDirection =
                                    (startPinId == ed::PinId(fromPinId) && endPinId == ed::PinId(toPinId)) ||
                                    (startPinId == ed::PinId(toPinId) && endPinId == ed::PinId(fromPinId));
                                if (!validDirection)
                                    return false;

                                if (ed::AcceptNewItem())
                                {
                                    state = true;
                                    if (markDirty)
                                        matEditor.dirty = true;
                                }
                                return true;
                            };

                            if (acceptLinkState(ui.mappingOutVectorPinId, ui.texturesInVectorPinId, ui.linkMappingActive, false))
                            {
                            }
                            else if (acceptLinkState(ui.principledOutBsdfPinId, ui.outputInSurfacePinId, ui.linkOutputActive, false))
                            {
                            }
                            else if (acceptLinkState(ui.colorOutPinId, ui.principledInEmissivePinId, ui.linkColorToEmissiveActive, true))
                            {
                                ui.linkEmissiveActive = false;
                                for (auto &dynamicColorNode : ui.dynamicColorNodes)
                                    dynamicColorNode.linkToEmissiveActive = false;
                                const glm::vec3 emissiveValue = ui.colorNodeValue * ui.colorNodeStrength;
                                gpuMat->setEmissiveFactor(emissiveValue);
                                cpuMat.emissiveFactor = emissiveValue;
                            }
                            else
                            {
                                bool acceptedDynamicColorLink = false;
                                for (auto &dynamicColorNode : ui.dynamicColorNodes)
                                {
                                    if (!acceptLinkState(dynamicColorNode.outputPinId, ui.principledInEmissivePinId, dynamicColorNode.linkToEmissiveActive, true))
                                        continue;

                                    ui.linkEmissiveActive = false;
                                    ui.linkColorToEmissiveActive = false;
                                    for (auto &otherDynamicColorNode : ui.dynamicColorNodes)
                                    {
                                        if (otherDynamicColorNode.outputPinId != dynamicColorNode.outputPinId)
                                            otherDynamicColorNode.linkToEmissiveActive = false;
                                    }

                                    const glm::vec3 emissiveValue = dynamicColorNode.color * dynamicColorNode.strength;
                                    gpuMat->setEmissiveFactor(emissiveValue);
                                    cpuMat.emissiveFactor = emissiveValue;
                                    acceptedDynamicColorLink = true;
                                    break;
                                }

                                if (acceptedDynamicColorLink)
                                    continue;

                                auto acceptTextureLink = [&](TextureSlotRow &row)
                                {
                                    const bool accepted = acceptLinkState(row.textureOutputPinId, row.principledInputPinId, *row.linkState, true);
                                    if (!accepted)
                                        return false;

                                    if (row.principledInputPinId == ui.principledInEmissivePinId)
                                    {
                                        ui.linkColorToEmissiveActive = false;
                                        for (auto &dynamicColorNode : ui.dynamicColorNodes)
                                            dynamicColorNode.linkToEmissiveActive = false;
                                    }

                                    return true;
                                };

                                for (auto &row : textureRows)
                                {
                                    if (acceptTextureLink(row))
                                        break;
                                }
                            }
                        }
                    }
                    ed::EndCreate();

                    const bool isDeleteActive = ed::BeginDelete();
                    if (isDeleteActive)
                    {
                        ed::LinkId deletedLinkId;
                        while (ed::QueryDeletedLink(&deletedLinkId))
                        {
                            if (deletedLinkId == ed::LinkId(ui.linkMappingId))
                            {
                                if (ed::AcceptDeletedItem())
                                    ui.linkMappingActive = false;
                                continue;
                            }

                            if (deletedLinkId == ed::LinkId(ui.linkOutputId))
                            {
                                if (ed::AcceptDeletedItem())
                                    ui.linkOutputActive = false;
                                continue;
                            }

                            if (deletedLinkId == ed::LinkId(ui.linkColorToEmissiveId))
                            {
                                if (ed::AcceptDeletedItem())
                                    ui.linkColorToEmissiveActive = false;
                                continue;
                            }

                            bool deletedDynamicColorLink = false;
                            for (auto &dynamicColorNode : ui.dynamicColorNodes)
                            {
                                if (deletedLinkId != ed::LinkId(dynamicColorNode.linkId))
                                    continue;

                                if (ed::AcceptDeletedItem())
                                    dynamicColorNode.linkToEmissiveActive = false;

                                deletedDynamicColorLink = true;
                                break;
                            }

                            if (deletedDynamicColorLink)
                                continue;

                            auto removeTextureLink = [&](TextureSlotRow &row)
                            {
                                if (deletedLinkId != ed::LinkId(row.linkId))
                                    return false;

                                if (ed::AcceptDeletedItem())
                                {
                                    applyTextureSlotChange(row, "");
                                    matEditor.dirty = true;
                                }
                                return true;
                            };

                            for (auto &row : textureRows)
                            {
                                if (removeTextureLink(row))
                                    break;
                            }
                        }
                    }
                    ed::EndDelete();

                    ed::Suspend();
                    const std::string createNodePopupName = "MaterialNodeCreatePopup##" + normalizedMatPath;
                    if (ed::ShowBackgroundContextMenu())
                        ImGui::OpenPopup(createNodePopupName.c_str());

                    if (ImGui::BeginPopup(createNodePopupName.c_str()))
                    {
                        if (ImGui::BeginMenu("Add node"))
                        {
                            if (ImGui::MenuItem("Color"))
                            {
                                MaterialEditorUIState::DynamicColorNode newColorNode;
                                newColorNode.nodeId = ui.nextDynamicColorNodeId++;
                                newColorNode.outputPinId = ui.nextDynamicColorPinId++;
                                newColorNode.linkId = ui.nextDynamicColorLinkId++;
                                newColorNode.color = glm::vec3(1.0f, 1.0f, 1.0f);
                                newColorNode.strength = 1.0f;
                                const ImVec2 canvasPosition = ed::ScreenToCanvas(ImGui::GetMousePos());
                                newColorNode.spawnPosition = glm::vec2(canvasPosition.x, canvasPosition.y);
                                newColorNode.pendingPlacement = true;
                                ui.dynamicColorNodes.push_back(newColorNode);
                            }
                            ImGui::EndMenu();
                        }
                        ImGui::EndPopup();
                    }

                    for (auto &popup : deferredTexturePopups)
                    {
                        if (!popup.row)
                            continue;

                        auto &row = *popup.row;
                        ImGui::PushID(row.label);

                        if (ui.openTexturePopup && ui.texturePopupSlot == row.label)
                        {
                            ImGui::OpenPopup(popup.popupName.c_str());
                            ui.openTexturePopup = false;
                        }

                        if (ImGui::BeginPopup(popup.popupName.c_str()))
                        {
                            ImGui::Text("Select Texture for %s", row.label);
                            ImGui::Separator();
                            ImGui::InputTextWithHint("##Search", "Search textures...", ui.textureFilter, sizeof(ui.textureFilter));
                            ImGui::SameLine();
                            if (ImGui::Button("X"))
                                ui.textureFilter[0] = '\0';

                            if (ImGui::Selectable("<Default>"))
                            {
                                applyTextureSlotChange(row, "");
                                matEditor.dirty = true;
                                ImGui::CloseCurrentPopup();
                            }

                            ImGui::BeginChild("TextureScroll", ImVec2(360.0f, 240.0f), true);
                            const std::string currentTextureReference = toMaterialTextureReferencePath(*row.cpuPath, projectRoot);
                            const auto textureAssetPaths = gatherProjectTextureAssets(*project, projectRoot);
                            for (const auto &texturePath : textureAssetPaths)
                            {
                                const std::string normalizedTexturePath = resolveTexturePathAgainstProjectRoot(texturePath, projectRoot);
                                const std::string textureReferencePath = toMaterialTextureReferencePath(normalizedTexturePath, projectRoot);
                                const std::string textureDisplayName = makeTextureAssetDisplayName(textureReferencePath);

                                if (ui.textureFilter[0] != '\0')
                                {
                                    const std::string filterLower = toLowerCopy(std::string(ui.textureFilter));
                                    if (toLowerCopy(textureReferencePath).find(filterLower) == std::string::npos &&
                                        toLowerCopy(textureDisplayName).find(filterLower) == std::string::npos)
                                        continue;
                                }

                                ImGui::PushID(textureReferencePath.c_str());
                                const auto previewTexture = ensureProjectTextureLoadedPreview(normalizedTexturePath, row.usage);
                                const auto previewDS = m_assetsPreviewSystem.getOrRequestTexturePreview(normalizedTexturePath, previewTexture);
                                const bool selected = currentTextureReference == textureReferencePath;
                                if (selected)
                                    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 200, 255, 255));

                                if (ImGui::ImageButton("##pick", (ImTextureID)(uintptr_t)previewDS, ImVec2(44.0f, 44.0f)))
                                {
                                    applyTextureSlotChange(row, textureReferencePath);
                                    matEditor.dirty = true;
                                    ImGui::CloseCurrentPopup();
                                }

                                if (selected)
                                    ImGui::PopStyleColor();

                                ImGui::SameLine();
                                ImGui::TextWrapped("%s", textureDisplayName.c_str());
                                ImGui::Separator();
                                ImGui::PopID();
                            }
                            ImGui::EndChild();

                            if (ImGui::Button("Close"))
                                ImGui::CloseCurrentPopup();

                            ImGui::EndPopup();
                        }

                        ImGui::PopID();
                    }

                    const std::string baseColorPopupName = "BaseColorPickerPopup##" + normalizedMatPath;
                    const std::string emissiveColorPopupName = "EmissiveColorPickerPopup##" + normalizedMatPath;

                    if (ui.openColorPopup)
                    {
                        if (ui.colorPopupSlot == 1)
                            ImGui::OpenPopup(baseColorPopupName.c_str());
                        else if (ui.colorPopupSlot == 2)
                            ImGui::OpenPopup(emissiveColorPopupName.c_str());
                        ui.openColorPopup = false;
                    }

                    if (ImGui::BeginPopup(baseColorPopupName.c_str()))
                    {
                        glm::vec4 pickerBaseColor = cpuMat.baseColorFactor;
                        if (ImGui::ColorPicker4("##BaseColorPicker",
                                                glm::value_ptr(pickerBaseColor),
                                                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB))
                        {
                            gpuMat->setBaseColorFactor(pickerBaseColor);
                            cpuMat.baseColorFactor = pickerBaseColor;
                            matEditor.dirty = true;
                        }

                        if (ImGui::Button("Close##BaseColorPicker"))
                        {
                            ui.colorPopupSlot = 0;
                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::EndPopup();
                    }

                    if (ImGui::BeginPopup(emissiveColorPopupName.c_str()))
                    {
                        glm::vec3 pickerEmissiveColor = cpuMat.emissiveFactor;
                        if (ImGui::ColorPicker3("##EmissiveColorPicker",
                                                glm::value_ptr(pickerEmissiveColor),
                                                ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                        {
                            gpuMat->setEmissiveFactor(pickerEmissiveColor);
                            cpuMat.emissiveFactor = pickerEmissiveColor;
                            matEditor.dirty = true;
                        }

                        if (ImGui::Button("Close##EmissiveColorPicker"))
                        {
                            ui.colorPopupSlot = 0;
                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::EndPopup();
                    }

                    if (!ImGui::IsPopupOpen(baseColorPopupName.c_str()) &&
                        !ImGui::IsPopupOpen(emissiveColorPopupName.c_str()))
                        ui.colorPopupSlot = 0;

                    ed::Resume();

                    ed::End();
                    ed::SetCurrentEditor(nullptr);
                }
            }
        }

        ImGui::End();

        if (!keepOpen && matEditor.dirty)
        {
            matEditor.confirmCloseRequested = true;
            keepOpen = true; // keep window alive
        }

        matEditor.open = keepOpen;
        if (!matEditor.open || closeRequested)
        {
            destroyNodeEditorState();
            it = m_openMaterialEditors.erase(it);
        }
        else
            ++it;
    }

    // Unsaved-changes confirmation for material editors (rendered outside the per-editor loop)
    for (auto it = m_openMaterialEditors.begin(); it != m_openMaterialEditors.end(); ++it)
    {
        auto &matEditor = *it;
        if (!matEditor.confirmCloseRequested)
            continue;

        const std::string popupId = "Unsaved Changes##Mat_" + matEditor.path.string();
        ImGui::OpenPopup(popupId.c_str());
        matEditor.confirmCloseRequested = false;

        if (ImGui::BeginPopupModal(popupId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("'%s' has unsaved changes.", matEditor.path.filename().string().c_str());
            ImGui::Text("Do you want to save before closing?");
            ImGui::Separator();

            auto project = m_currentProject.lock();
            const std::filesystem::path projectRoot = project ? std::filesystem::path(project->fullPath) : std::filesystem::path{};
            const std::string normalizedMatPath = resolveMaterialPathAgainstProjectRoot(matEditor.path.string(), projectRoot);

            if (ImGui::Button("Save"))
            {
                auto materialRecordIt = project ? project->cache.materialsByPath.find(normalizedMatPath) : project->cache.materialsByPath.end();
                if (project && materialRecordIt != project->cache.materialsByPath.end())
                    saveMaterialToDisk(matEditor.path, materialRecordIt->second.cpuData);
                matEditor.open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard"))
            {
                matEditor.open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
        break; // only one modal at a time
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

    if (!keepOpen && isDirty)
    {
        m_documentConfirmCloseRequested = true;
        // keep the window alive until user decides
    }
    else if (!keepOpen)
    {
        m_showDocumentWindow = false;
        m_openDocumentPath.clear();
        m_openDocumentSavedText.clear();
        m_textEditor.SetText("");
    }

    if (m_documentConfirmCloseRequested)
    {
        ImGui::OpenPopup("Unsaved Changes##Doc");
        m_documentConfirmCloseRequested = false;
    }

    if (ImGui::BeginPopupModal("Unsaved Changes##Doc", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("'%s' has unsaved changes.", m_openDocumentPath.filename().string().c_str());
        ImGui::Text("Do you want to save before closing?");
        ImGui::Separator();

        if (ImGui::Button("Save"))
        {
            saveOpenDocument();
            m_showDocumentWindow = false;
            m_openDocumentPath.clear();
            m_openDocumentSavedText.clear();
            m_textEditor.SetText("");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard"))
        {
            m_showDocumentWindow = false;
            m_openDocumentPath.clear();
            m_openDocumentSavedText.clear();
            m_textEditor.SetText("");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
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
        m_lastScrolledMeshSlot.reset();
        m_isColliderHandleActive = false;
        m_isColliderHandleHovered = false;
        m_activeColliderHandle = ColliderHandleType::NONE;
    }

    m_selectedEntity = entity;

    if (m_selectedEntity)
    {
        clearSelectedUIElement();
        m_detailsContext = DetailsContext::Entity;
    }

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

    const bool isCtrlDown = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
    const bool isShiftDown = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
    const bool assetsWindowConsumesDelete = m_showAssetsWindow && m_assetsWindow && m_assetsWindow->hasKeyboardFocus();
    const bool shortcutBlockedByTextInput = io.WantTextInput || ImGui::GetActiveID() != 0;

    if (isCtrlDown && !shortcutBlockedByTextInput && ImGui::IsKeyPressed(ImGuiKey_Z, false))
    {
        if (isShiftDown)
            performRedoAction();
        else
            performUndoAction();
    }

    if (isCtrlDown && !shortcutBlockedByTextInput && ImGui::IsKeyPressed(ImGuiKey_Y, false))
        performRedoAction();

    if (isCtrlDown && !shortcutBlockedByTextInput && ImGui::IsKeyPressed(ImGuiKey_C, false))
        performCopyAction();

    if (isCtrlDown && !shortcutBlockedByTextInput && ImGui::IsKeyPressed(ImGuiKey_V, false))
        performPasteAction();

    bool sceneMutated = false;

    if (!assetsWindowConsumesDelete && ImGui::IsKeyPressed(ImGuiKey_Delete) && m_selectedEntity && m_scene && m_currentMode == EditorMode::EDIT)
    {
        if (m_scene->destroyEntity(m_selectedEntity))
        {
            setSelectedEntity(nullptr);
            sceneMutated = true;
        }
    }
    else if (!assetsWindowConsumesDelete && ImGui::IsKeyPressed(ImGuiKey_Delete) && m_hasSelectedUIElement && m_scene && m_currentMode == EditorMode::EDIT)
    {
        const auto &texts = m_scene->getUITexts();
        const auto &buttons = m_scene->getUIButtons();
        const auto &billboards = m_scene->getBillboards();
        bool removedElement = false;

        switch (m_uiSelectionType)
        {
        case UISelectionType::Text:
            if (m_selectedUIElementIndex < texts.size())
            {
                m_scene->removeUIText(texts[m_selectedUIElementIndex].get());
                removedElement = true;
            }
            break;
        case UISelectionType::Button:
            if (m_selectedUIElementIndex < buttons.size())
            {
                m_scene->removeUIButton(buttons[m_selectedUIElementIndex].get());
                removedElement = true;
            }
            break;
        case UISelectionType::Billboard:
            if (m_selectedUIElementIndex < billboards.size())
            {
                m_scene->removeBillboard(billboards[m_selectedUIElementIndex].get());
                removedElement = true;
            }
            break;
        case UISelectionType::None:
        default:
            break;
        }

        if (removedElement)
        {
            clearSelectedUIElement();
            sceneMutated = true;
        }
    }

    if (sceneMutated)
        captureSceneActionSnapshot("Delete");

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

void Editor::resetSceneActionHistory()
{
    if (!m_scene)
        return;

    if (!m_sceneActionHistory.reset(*m_scene, "Initial state"))
        VX_EDITOR_WARNING_STREAM("Failed to initialize scene action history.\n");
}

void Editor::captureSceneActionSnapshot(const std::string &label)
{
    if (!m_scene || m_currentMode != EditorMode::EDIT)
        return;

    if (!m_sceneActionHistory.capture(*m_scene, label))
        VX_EDITOR_WARNING_STREAM("Failed to capture scene action snapshot: " << label << '\n');
}

bool Editor::performUndoAction()
{
    if (!m_scene)
        return false;
    if (m_currentMode != EditorMode::EDIT)
    {
        m_notificationManager.showWarning("Undo is available only in Edit mode");
        return false;
    }

    if (!m_sceneActionHistory.canUndo())
    {
        m_notificationManager.showWarning("Nothing to undo");
        return false;
    }

    if (!m_sceneActionHistory.undo(*m_scene))
    {
        m_notificationManager.showError("Undo failed");
        return false;
    }

    setSelectedEntity(nullptr);
    m_selectedMeshSlot.reset();
    clearSelectedUIElement();
    m_hasPendingObjectPick = false;
    invalidateModelDetailsCache();
    m_notificationManager.showInfo("Undo");
    return true;
}

bool Editor::performRedoAction()
{
    if (!m_scene)
        return false;
    if (m_currentMode != EditorMode::EDIT)
    {
        m_notificationManager.showWarning("Redo is available only in Edit mode");
        return false;
    }

    if (!m_sceneActionHistory.canRedo())
    {
        m_notificationManager.showWarning("Nothing to redo");
        return false;
    }

    if (!m_sceneActionHistory.redo(*m_scene))
    {
        m_notificationManager.showError("Redo failed");
        return false;
    }

    setSelectedEntity(nullptr);
    m_selectedMeshSlot.reset();
    clearSelectedUIElement();
    m_hasPendingObjectPick = false;
    invalidateModelDetailsCache();
    m_notificationManager.showInfo("Redo");
    return true;
}

bool Editor::performCopyAction()
{
    if (!m_scene || !m_selectedEntity)
        return false;
    if (m_currentMode != EditorMode::EDIT)
    {
        m_notificationManager.showWarning("Copy is available only in Edit mode");
        return false;
    }

    if (!m_entityClipboard.copySelectedEntity(*m_scene, m_selectedEntity->getId()))
    {
        m_notificationManager.showError("Copy failed");
        return false;
    }

    m_notificationManager.showInfo("Copied: " + m_selectedEntity->getName());
    return true;
}

bool Editor::performPasteAction()
{
    if (!m_scene)
        return false;
    if (m_currentMode != EditorMode::EDIT)
    {
        m_notificationManager.showWarning("Paste is available only in Edit mode");
        return false;
    }

    if (!m_entityClipboard.hasEntity())
    {
        m_notificationManager.showWarning("Clipboard is empty");
        return false;
    }

    std::uint32_t newEntityId = 0u;
    if (!m_entityClipboard.pasteEntity(*m_scene, &newEntityId))
    {
        m_notificationManager.showError("Paste failed");
        return false;
    }

    setSelectedEntity(nullptr);
    clearSelectedUIElement();

    if (engine::Entity *pastedEntity = m_scene->getEntityById(newEntityId))
        setSelectedEntity(pastedEntity);

    invalidateModelDetailsCache();
    captureSceneActionSnapshot("Paste entity");
    m_notificationManager.showSuccess("Pasted entity");
    return true;
}

void Editor::openMaterialEditor(const std::filesystem::path &path)
{
    std::filesystem::path normalizedPath = path;
    if (auto project = m_currentProject.lock(); project)
        normalizedPath = resolveMaterialPathAgainstProjectRoot(path.string(), std::filesystem::path(project->fullPath));

    auto destroyNodeEditorStateForPath = [&](const std::filesystem::path &materialPath)
    {
        const std::string normalizedMaterialPath = materialPath.lexically_normal().string();
        auto stateIt = m_materialEditorUiState.find(normalizedMaterialPath);
        if (stateIt == m_materialEditorUiState.end())
            return;

        if (stateIt->second.nodeEditorContext)
        {
            ed::DestroyEditor(stateIt->second.nodeEditorContext);
            stateIt->second.nodeEditorContext = nullptr;
        }

        m_materialEditorUiState.erase(stateIt);
    };

    for (const auto &mat : m_openMaterialEditors)
    {
        if (mat.path == normalizedPath)
        {
            m_openMaterialEditors.clear();
            OpenMaterialEditor editor;
            editor.path = normalizedPath;
            editor.open = true;
            editor.dirty = mat.dirty;
            m_openMaterialEditors.push_back(std::move(editor));
            return;
        }
    }

    for (const auto &mat : m_openMaterialEditors)
        destroyNodeEditorStateForPath(mat.path);
    m_openMaterialEditors.clear();

    OpenMaterialEditor editor;
    editor.path = normalizedPath;
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

engine::Texture::SharedPtr Editor::ensureProjectTextureLoadedPreview(const std::string &texturePath, TextureUsage usage)
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

    const TextureUsage previewUsage = isDataTextureUsage(usage) ? TextureUsage::PreviewData : TextureUsage::PreviewColor;
    if (auto cached = record.getGpuVariant(previewUsage))
        return cached;

    auto texture = engine::AssetsLoader::loadTextureGPU(
        normalizedTexturePath,
        getLdrTextureFormat(previewUsage));
    if (!texture)
    {
        VX_EDITOR_WARNING_STREAM("Failed to load preview texture: " << normalizedTexturePath << '\n');
        return nullptr;
    }

    record.setGpuVariant(previewUsage, texture);
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
    json["ior"] = cpuMaterial.ior;
    json["alpha_cutoff"] = cpuMaterial.alphaCutoff;
    json["flags"] = cpuMaterial.flags;
    json["uv_scale"] = {cpuMaterial.uvScale.x, cpuMaterial.uvScale.y};
    json["uv_location"] = {cpuMaterial.uvOffset.x, cpuMaterial.uvOffset.y};
    json["uv_rotation"] = cpuMaterial.uvRotation;
    json["uv_offset"] = {cpuMaterial.uvOffset.x, cpuMaterial.uvOffset.y};

    if (!cpuMaterial.customExpression.empty())
        json["custom_expression"] = cpuMaterial.customExpression;
    if (!cpuMaterial.customShaderHash.empty())
        json["custom_shader_hash"] = cpuMaterial.customShaderHash;

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
    const std::string normalizedMaterialPath = resolveMaterialPathAgainstProjectRoot(path.string(), projectRoot);
    normalizeMaterialTexturePaths(cpuMaterial, path, projectRoot);
    sanitizeMaterialCpuData(cpuMaterial, false);

    auto &record = project->cache.materialsByPath[normalizedMaterialPath];
    record.path = normalizedMaterialPath;
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
    record.gpu->setIor(cpuMaterial.ior);
    record.gpu->setAlphaCutoff(cpuMaterial.alphaCutoff);
    record.gpu->setFlags(cpuMaterial.flags);
    record.gpu->setUVScale(cpuMaterial.uvScale);
    record.gpu->setUVOffset(cpuMaterial.uvOffset);
    record.gpu->setUVRotation(cpuMaterial.uvRotation);

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

    const std::filesystem::path projectRoot = std::filesystem::path(project->fullPath);
    const std::string normalizedMaterialPath = resolveMaterialPathAgainstProjectRoot(materialPath, projectRoot);

    auto it = project->cache.materialsByPath.find(normalizedMaterialPath);
    if (it != project->cache.materialsByPath.end() && it->second.gpu)
        return it->second.gpu;

    auto loadAndCacheMaterial = [&](const std::string &candidatePath) -> engine::Material::SharedPtr
    {
        auto materialAsset = engine::AssetsLoader::loadMaterial(candidatePath);
        if (!materialAsset.has_value())
            return nullptr;

        auto cpuMaterial = materialAsset.value().material;
        if (cpuMaterial.name.empty())
            cpuMaterial.name = std::filesystem::path(candidatePath).stem().string();

        normalizeMaterialTexturePaths(cpuMaterial, std::filesystem::path(candidatePath), projectRoot);
        sanitizeMaterialCpuData(cpuMaterial, false);

        auto &record = project->cache.materialsByPath[normalizedMaterialPath];
        record.path = normalizedMaterialPath;
        record.cpuData = cpuMaterial;

        if (!record.gpu)
            record.gpu = engine::Material::create(ensureProjectTextureLoaded(cpuMaterial.albedoTexture, TextureUsage::Color));
        if (!record.gpu)
            return nullptr;

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
        record.gpu->setIor(cpuMaterial.ior);
        record.gpu->setAlphaCutoff(cpuMaterial.alphaCutoff);
        record.gpu->setFlags(cpuMaterial.flags);
        record.gpu->setUVScale(cpuMaterial.uvScale);
        record.gpu->setUVOffset(cpuMaterial.uvOffset);
        record.gpu->setUVRotation(cpuMaterial.uvRotation);

        record.texture = ensureProjectTextureLoaded(cpuMaterial.albedoTexture, TextureUsage::Color);
        return record.gpu;
    };

    if (auto loadedMaterial = loadAndCacheMaterial(normalizedMaterialPath))
        return loadedMaterial;

    if (materialPath != normalizedMaterialPath)
        if (auto loadedMaterial = loadAndCacheMaterial(materialPath))
            return loadedMaterial;

    it = project->cache.materialsByPath.find(normalizedMaterialPath);
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
                                                   resolved,
                                                   textureSearchDirectory,
                                                   true);
    };

    auto collectUnresolvedPath = [&](const std::string &texturePath)
    {
        bool resolved = true;
        resolveTexturePathForMaterialExport(texturePath,
                                            modelDirectory,
                                            projectRoot,
                                            textureSearchDirectory,
                                            m_modelTextureManualOverrides,
                                            resolved,
                                            textureSearchDirectory,
                                            true);

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
        sanitizeMaterialCpuData(material, true);
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

        auto material = materialAsset.value().material;
        sanitizeMaterialCpuData(material, true);
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
        sanitizeMaterialCpuData(candidate, true);
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
    auto project = m_currentProject.lock();
    if (!project)
    {
        VX_EDITOR_WARNING_STREAM("Material auto-apply failed. No active project.");
        return false;
    }

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
    const std::filesystem::path projectRoot = std::filesystem::path(project->fullPath);

    if (slotLimit == 0)
        return false;

    size_t appliedCount = 0;
    size_t failedLoads = 0;

    for (size_t slot = 0; slot < slotLimit; ++slot)
    {
        const std::string materialPath = resolveMaterialPathAgainstProjectRoot(perMeshMaterialPaths[slot], projectRoot);
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
        sanitizeMaterialCpuData(candidate, true);

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
            const std::string resolvedTexturePathForField = resolveTexturePathForMaterialExport(texturePath,
                                                                                                modelDirectory,
                                                                                                projectRoot,
                                                                                                textureSearchDirectory,
                                                                                                textureOverrides,
                                                                                                resolved,
                                                                                                exportDirectory,
                                                                                                true);
            texturePath = toMaterialTextureReferencePath(resolvedTexturePathForField, projectRoot, exportDirectory, false);
            const std::filesystem::path resolvedTexturePath = std::filesystem::path(resolveTexturePathAgainstProjectRoot(texturePath, projectRoot)).lexically_normal();
            auto textureType = readSerializedAssetType(resolvedTexturePath);
            const bool isSerializedTexture = textureType.has_value() && textureType.value() == engine::Asset::AssetType::TEXTURE;

            if (!resolved || !isSerializedTexture)
            {
                if (!originalPath.empty())
                    unresolvedTexturePaths.insert(originalPath);
                texturePath.clear();
            }
            else if (texturePath.empty() && !originalPath.empty())
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
                auto previewOverrides = m_modelTextureManualOverrides;

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
                                                                                                true);
                    if (remappedResolved && !resolvedPreviewPath.empty())
                    {
                        remappedPathString = resolvedPreviewPath;
                        previewOverrides[unresolvedPath] = remappedPathString;
                    }

                    auto overrideIt = m_modelTextureManualOverrides.find(unresolvedPath);
                    if (overrideIt == m_modelTextureManualOverrides.end() || overrideIt->second != remappedPathString)
                    {
                        m_modelTextureManualOverrides[unresolvedPath] = remappedPathString;
                        overridesChanged = true;
                    }

                    if (remappedResolved)
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
                                                                                    previewResolved,
                                                                                    textureSearchDirectory,
                                                                                    true);
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
    auto project = m_currentProject.lock();
    if (!project)
    {
        VX_EDITOR_WARNING_STREAM("Material apply failed. No active project.");
        return false;
    }

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

    const std::string normalizedMaterialPath = resolveMaterialPathAgainstProjectRoot(materialPath, std::filesystem::path(project->fullPath));
    auto material = ensureMaterialLoaded(normalizedMaterialPath);
    if (!material)
    {
        VX_EDITOR_ERROR_STREAM("Material apply failed. Could not load material: " << normalizedMaterialPath);
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
            staticMeshComponent->setMaterialOverridePath(resolvedSlot.value(), normalizedMaterialPath);
        }
        else
        {
            skeletalMeshComponent->setMaterialOverride(resolvedSlot.value(), material);
            skeletalMeshComponent->setMaterialOverridePath(resolvedSlot.value(), normalizedMaterialPath);
        }

        VX_EDITOR_INFO_STREAM("Applied material '" << normalizedMaterialPath << "' to entity '" << m_selectedEntity->getName() << "' slot " << resolvedSlot.value());
        return true;
    }

    for (size_t index = 0; index < slotCount; ++index)
    {
        if (staticMeshComponent)
        {
            staticMeshComponent->setMaterialOverride(index, material);
            staticMeshComponent->setMaterialOverridePath(index, normalizedMaterialPath);
        }
        else
        {
            skeletalMeshComponent->setMaterialOverride(index, material);
            skeletalMeshComponent->setMaterialOverridePath(index, normalizedMaterialPath);
        }
    }

    VX_EDITOR_INFO_STREAM("Applied material '" << normalizedMaterialPath << "' to all slots of entity '" << m_selectedEntity->getName() << "'.");
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
    std::vector<engine::CPUMesh> spawnedMeshes = model->meshes;
    // Imported mesh materials are kept in the asset for export workflows, but
    // editor-spawned entities start without bound materials until explicitly applied.
    for (auto &mesh : spawnedMeshes)
        mesh.material = {};

    if (model->skeleton.has_value())
    {
        auto *skeletalMeshComponent = entity->addComponent<engine::SkeletalMeshComponent>(spawnedMeshes, model->skeleton.value());
        skeletalMeshComponent->setAssetPath(assetPath);

        if (!model->animations.empty())
        {
            auto *animatorComponent = entity->addComponent<engine::AnimatorComponent>();
            animatorComponent->setAnimations(model->animations, &skeletalMeshComponent->getSkeleton());
            animatorComponent->setSelectedAnimationIndex(0);
        }
    }
    else
    {
        auto *staticMeshComponent = entity->addComponent<engine::StaticMeshComponent>(spawnedMeshes);
        staticMeshComponent->setAssetPath(assetPath);
    }

    if (auto transform = entity->getComponent<engine::Transform3DComponent>())
    {
        glm::vec3 spawnPosition(0.0f);

        if (m_editorCamera)
            spawnPosition = m_editorCamera->getPosition() + m_editorCamera->getForward() * 3.0f;

        transform->setPosition(spawnPosition);
    }

    setSelectedEntity(entity.get());
    captureSceneActionSnapshot("Spawn model");

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
    captureSceneActionSnapshot("Add primitive");

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
    captureSceneActionSnapshot("Add empty entity");
    VX_EDITOR_INFO_STREAM("Added empty entity: " << entity->getName());
    m_notificationManager.showSuccess("Added empty entity");
}

void Editor::addDefaultCharacterEntity(const std::string &name)
{
    if (!m_scene)
    {
        VX_EDITOR_ERROR_STREAM("Add default character failed. Scene is null.");
        return;
    }

    auto entity = m_scene->addEntity(name.empty() ? "Character" : name);
    if (!entity)
    {
        VX_EDITOR_ERROR_STREAM("Failed to create default character entity.");
        return;
    }

    auto *transform = entity->getComponent<engine::Transform3DComponent>();
    if (transform)
    {
        glm::vec3 spawnPosition(0.0f);
        if (m_editorCamera)
            spawnPosition = m_editorCamera->getPosition() + m_editorCamera->getForward() * 3.0f;

        transform->setPosition(spawnPosition);
        transform->setScale(glm::vec3(1.0f, 1.8f, 1.0f));
    }

    entity->addComponent<engine::CharacterMovementComponent>(m_scene.get(), 0.35f, 1.0f);

    setSelectedEntity(entity.get());
    captureSceneActionSnapshot("Add character");

    VX_EDITOR_INFO_STREAM("Added default character entity: " << entity->getName());
    m_notificationManager.showSuccess("Added default character");
}

engine::Camera::SharedPtr Editor::getCurrentCamera()
{
    return m_editorCamera;
}

void Editor::addOnViewportChangedCallback(const std::function<void(uint32_t width, uint32_t height)> &function)
{
    m_onViewportWindowResized.push_back(function);
}

void Editor::addOnGameViewportChangedCallback(const std::function<void(uint32_t width, uint32_t height)> &function)
{
    m_onGameViewportWindowResized.push_back(function);
}

glm::vec2 Editor::viewportPixelToNdc(const ImVec2 &pixelPos, const ImVec2 &imageMin, const ImVec2 &imageMax) const
{
    const float width = std::max(1.0f, imageMax.x - imageMin.x);
    const float height = std::max(1.0f, imageMax.y - imageMin.y);

    const float u = std::clamp((pixelPos.x - imageMin.x) / width, 0.0f, 1.0f);
    const float v = std::clamp((pixelPos.y - imageMin.y) / height, 0.0f, 1.0f);

    return glm::vec2(
        std::clamp(u * 2.0f - 1.0f, -1.0f, 1.0f),
        std::clamp(1.0f - v * 2.0f, -1.0f, 1.0f));
}

glm::vec2 Editor::ndcToViewportPixel(const glm::vec2 &ndcPos, const ImVec2 &imageMin, const ImVec2 &imageMax) const
{
    const float width = std::max(1.0f, imageMax.x - imageMin.x);
    const float height = std::max(1.0f, imageMax.y - imageMin.y);

    const float u = std::clamp((ndcPos.x + 1.0f) * 0.5f, 0.0f, 1.0f);
    const float v = std::clamp((1.0f - ndcPos.y) * 0.5f, 0.0f, 1.0f);

    return glm::vec2(
        imageMin.x + u * width,
        imageMin.y + v * height);
}

glm::vec2 Editor::snapNdcToGrid(const glm::vec2 &ndcPos) const
{
    const float step = std::clamp(m_uiPlacementGridStepNdc, 0.02f, 1.0f);
    if (!m_uiPlacementSnapToGrid || step <= std::numeric_limits<float>::epsilon())
        return glm::clamp(ndcPos, glm::vec2(-1.0f), glm::vec2(1.0f));

    const float snappedX = std::round(ndcPos.x / step) * step;
    const float snappedY = std::round(ndcPos.y / step) * step;
    return glm::clamp(glm::vec2(snappedX, snappedY), glm::vec2(-1.0f), glm::vec2(1.0f));
}

glm::vec3 Editor::computeBillboardPlacementWorldPosition(const glm::vec2 &ndcPos) const
{
    if (!m_editorCamera)
        return glm::vec3(ndcPos.x, ndcPos.y, 0.0f);

    const glm::mat4 view = m_editorCamera->getViewMatrix();
    const glm::mat4 projection = m_editorCamera->getProjectionMatrix();
    const glm::mat4 invViewProjection = glm::inverse(projection * view);

    glm::vec4 nearPoint = invViewProjection * glm::vec4(ndcPos.x, ndcPos.y, -1.0f, 1.0f);
    glm::vec4 farPoint = invViewProjection * glm::vec4(ndcPos.x, ndcPos.y, 1.0f, 1.0f);

    if (std::abs(nearPoint.w) > std::numeric_limits<float>::epsilon())
        nearPoint /= nearPoint.w;
    if (std::abs(farPoint.w) > std::numeric_limits<float>::epsilon())
        farPoint /= farPoint.w;

    glm::vec3 rayDirection = glm::vec3(farPoint - nearPoint);
    if (glm::length(rayDirection) <= std::numeric_limits<float>::epsilon())
        rayDirection = m_editorCamera->getForward();
    else
        rayDirection = glm::normalize(rayDirection);

    const float distance = std::max(0.1f, m_uiBillboardPlacementDistance);
    return m_editorCamera->getPosition() + rayDirection * distance;
}

void Editor::clearSelectedUIElement()
{
    m_hasSelectedUIElement = false;
    m_uiSelectionType = UISelectionType::None;
    m_selectedUIElementIndex = 0;
}

bool Editor::placeUIElementAtViewportPosition(const ImVec2 &pixelPos, const ImVec2 &imageMin, const ImVec2 &imageMax)
{
    if (!m_scene || m_uiPlacementTool == UIPlacementTool::None)
        return false;

    glm::vec2 ndc = viewportPixelToNdc(pixelPos, imageMin, imageMax);
    ndc = snapNdcToGrid(ndc);

    switch (m_uiPlacementTool)
    {
    case UIPlacementTool::Text:
    {
        auto *text = m_scene->addUIText();
        if (!text)
            return false;

        text->setPosition(ndc);
        text->setText("Text");
        text->loadFont("./resources/fonts/JetBrainsMono-Regular.ttf");

        m_hasSelectedUIElement = true;
        m_uiSelectionType = UISelectionType::Text;
        m_selectedUIElementIndex = m_scene->getUITexts().empty() ? 0 : (m_scene->getUITexts().size() - 1);
        m_notificationManager.showInfo("Placed UIText");
        return true;
    }
    case UIPlacementTool::Button:
    {
        auto *button = m_scene->addUIButton();
        if (!button)
            return false;

        const glm::vec2 size = button->getSize();
        glm::vec2 topLeft = ndc - size * 0.5f;
        topLeft = glm::clamp(topLeft, glm::vec2(-1.0f), glm::vec2(1.0f) - size);
        button->setPosition(topLeft);
        button->setLabel("Button");
        button->loadFont("./resources/fonts/JetBrainsMono-Regular.ttf");

        m_hasSelectedUIElement = true;
        m_uiSelectionType = UISelectionType::Button;
        m_selectedUIElementIndex = m_scene->getUIButtons().empty() ? 0 : (m_scene->getUIButtons().size() - 1);
        m_notificationManager.showInfo("Placed UIButton");
        return true;
    }
    case UIPlacementTool::Billboard:
    {
        auto *billboard = m_scene->addBillboard();
        if (!billboard)
            return false;

        billboard->setWorldPosition(computeBillboardPlacementWorldPosition(ndc));

        m_hasSelectedUIElement = true;
        m_uiSelectionType = UISelectionType::Billboard;
        m_selectedUIElementIndex = m_scene->getBillboards().empty() ? 0 : (m_scene->getBillboards().size() - 1);
        m_notificationManager.showInfo("Placed Billboard");
        return true;
    }
    case UIPlacementTool::None:
    default:
        break;
    }

    return false;
}

void Editor::drawBenchmark()
{
    if (!m_showBenchmark)
        return;

    ImGui::SetNextWindowSize(ImVec2(540, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(80, 80), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Benchmark", &m_showBenchmark))
    {
        ImGui::End();
        return;
    }

    float fps = ImGui::GetIO().Framerate;
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Frame time: %.3f ms", 1000.0f / fps);
    ImGui::Text("VSync: %s", engine::RenderQualitySettings::getInstance().enableVSync ? "ON" : "OFF");
    ImGui::TextDisabled("FPS above is full editor loop; detailed CPU/GPU numbers below are render-graph only.");
    if (m_isGameViewportVisible)
        ImGui::TextDisabled("Game Viewport graph is active; table below shows only main Viewport graph.");

    auto &engineConfig = engine::EngineConfig::instance();
    bool detailedRenderProfiling = engineConfig.getDetailedRenderProfilingEnabled();
    if (ImGui::Checkbox("Detailed Render Profiling", &detailedRenderProfiling))
    {
        engineConfig.setDetailedRenderProfilingEnabled(detailedRenderProfiling);
        if (!engineConfig.save())
            VX_EDITOR_WARNING_STREAM("Failed to persist detailed render profiling setting to engine config\n");
    }
    ImGui::SetItemTooltip("OFF: minimal benchmark (FPS + frame time). ON: full render-graph CPU/GPU timings.");

    // --- Summary (always shown when profiling is enabled) ---
    ImGui::Separator();
    ImGui::Text("VRAM: %ld MB   RAM: %ld MB", core::VulkanContext::getContext()->getDevice()->getTotalAllocatedVRAM(),
                                               core::VulkanContext::getContext()->getDevice()->getTotalUsedRAM());
    ImGui::Text("Draw calls: %u", m_renderGraphProfilingData.totalDrawCalls);
    ImGui::Separator();

    ImGui::Text("CPU prepare frame : %.3f ms", m_renderGraphProfilingData.cpuPrepareFrameMs);
    ImGui::SetItemTooltip("Fence wait + command pool reset + image acquire + recompile");
    ImGui::TextDisabled("  incl. GPU sync wait: %.3f ms", m_renderGraphProfilingData.cpuWaitForFenceMs);
    ImGui::Text("CPU actual frame  : %.3f ms", m_renderGraphProfilingData.cpuActualFrameMs);
    ImGui::SetItemTooltip("All pass recording + primary CB end + submit + present");

    if (m_renderGraphProfilingData.gpuTimingAvailable)
        ImGui::Text("GPU actual frame  : %.3f ms", m_renderGraphProfilingData.gpuActualFrameMs);
    else
        ImGui::TextDisabled("GPU actual frame  : unavailable");

    if (!detailedRenderProfiling)
    {
        ImGui::TextDisabled("Enable Detailed Render Profiling for per-pass breakdown.");
    }
    else
    {
        ImGui::Separator();
        if (ImGui::BeginTable("RenderGraphPassStats", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupColumn("Pass");
            ImGui::TableSetupColumn("Exec");
            ImGui::TableSetupColumn("Draws");
            ImGui::TableSetupColumn("CPU (ms)");
            ImGui::TableSetupColumn("GPU (ms)");
            ImGui::TableSetupScrollFreeze(0, 1);
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
    }

    ImGui::End();
}

void Editor::drawDevTools()
{
    if (!m_showDevTools)
        return;

    ImGui::SetNextWindowSize(ImVec2(420, 280), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(120, 120), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Dev Tools", &m_showDevTools))
    {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Engine Shaders");
    ImGui::TextDisabled("Recompiles all GLSL sources in resources/shaders/ to SPIR-V,");
    ImGui::TextDisabled("then reloads all GPU pipelines. Use after editing engine shaders.");

    ImGui::Spacing();

    if (ImGui::Button("Reload Engine Shaders", ImVec2(200, 0)))
    {
        m_devToolsShaderErrors.clear();
        m_devToolsLastCompiledCount = -1;

        if (m_reloadShadersCallback)
            m_devToolsLastCompiledCount = static_cast<int>(m_reloadShadersCallback(&m_devToolsShaderErrors));

        m_pendingShaderReloadRequest = true;
    }

    if (m_devToolsLastCompiledCount >= 0)
    {
        ImGui::SameLine();
        if (m_devToolsShaderErrors.empty())
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "OK (%d compiled)", m_devToolsLastCompiledCount);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%d errors", static_cast<int>(m_devToolsShaderErrors.size()));
    }

    if (!m_devToolsShaderErrors.empty())
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Compile errors:");
        ImGui::BeginChild("##shader_errors", ImVec2(0, 120), true);
        for (const auto &err : m_devToolsShaderErrors)
            ImGui::TextUnformatted(err.c_str());
        ImGui::EndChild();
    }

    ImGui::End();
}

void Editor::drawUITools()
{
    if (!m_showUITools)
        return;

    ImGui::Begin("UI Tools", &m_showUITools);

    if (!m_scene)
    {
        ImGui::TextDisabled("Scene is not available.");
        return ImGui::End();
    }

    ImGui::Checkbox("Show Placement Grid", &m_uiPlacementGridEnabled);
    ImGui::SameLine();
    ImGui::Checkbox("Snap To Grid", &m_uiPlacementSnapToGrid);
    ImGui::SliderFloat("Grid Step (NDC)", &m_uiPlacementGridStepNdc, 0.02f, 0.5f, "%.2f");
    ImGui::SliderFloat("Billboard Distance", &m_uiBillboardPlacementDistance, 0.5f, 100.0f, "%.1f");

    int toolIndex = static_cast<int>(m_uiPlacementTool);
    const char *toolItems[] = {"None", "UIText", "UIButton", "Billboard"};
    if (ImGui::Combo("Placement Tool", &toolIndex, toolItems, IM_ARRAYSIZE(toolItems)))
        m_uiPlacementTool = static_cast<UIPlacementTool>(toolIndex);

    if (m_uiPlacementTool != UIPlacementTool::None)
        ImGui::TextDisabled("Click inside Viewport to place selected UI element.");

    ImGui::Separator();

    if (ImGui::Button("Add UIText At Center"))
    {
        auto *text = m_scene->addUIText();
        if (text)
        {
            text->setPosition(glm::vec2(-0.1f, 0.0f));
            text->setText("Text");
            text->loadFont("./resources/fonts/JetBrainsMono-Regular.ttf");
            m_hasSelectedUIElement = true;
            m_uiSelectionType = UISelectionType::Text;
            m_selectedUIElementIndex = m_scene->getUITexts().empty() ? 0 : (m_scene->getUITexts().size() - 1);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Add UIButton At Center"))
    {
        auto *button = m_scene->addUIButton();
        if (button)
        {
            const glm::vec2 size = button->getSize();
            button->setPosition(glm::vec2(-size.x * 0.5f, -size.y * 0.5f));
            button->setLabel("Button");
            button->loadFont("./resources/fonts/JetBrainsMono-Regular.ttf");
            m_hasSelectedUIElement = true;
            m_uiSelectionType = UISelectionType::Button;
            m_selectedUIElementIndex = m_scene->getUIButtons().empty() ? 0 : (m_scene->getUIButtons().size() - 1);
        }
    }

    if (ImGui::Button("Add Billboard In Front Of Camera"))
    {
        auto *billboard = m_scene->addBillboard();
        if (billboard)
        {
            const glm::vec2 centerNdc = snapNdcToGrid(glm::vec2(0.0f));
            billboard->setWorldPosition(computeBillboardPlacementWorldPosition(centerNdc));
            m_hasSelectedUIElement = true;
            m_uiSelectionType = UISelectionType::Billboard;
            m_selectedUIElementIndex = m_scene->getBillboards().empty() ? 0 : (m_scene->getBillboards().size() - 1);
        }
    }

    const auto &texts = m_scene->getUITexts();
    const auto &buttons = m_scene->getUIButtons();
    const auto &billboards = m_scene->getBillboards();

    if (m_hasSelectedUIElement)
    {
        bool selectionIsValid = false;
        switch (m_uiSelectionType)
        {
        case UISelectionType::Text:
            selectionIsValid = m_selectedUIElementIndex < texts.size();
            break;
        case UISelectionType::Button:
            selectionIsValid = m_selectedUIElementIndex < buttons.size();
            break;
        case UISelectionType::Billboard:
            selectionIsValid = m_selectedUIElementIndex < billboards.size();
            break;
        case UISelectionType::None:
        default:
            selectionIsValid = false;
            break;
        }

        if (!selectionIsValid)
            clearSelectedUIElement();
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("UIText", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (std::size_t i = 0; i < texts.size(); ++i)
        {
            const auto &text = texts[i];
            if (!text)
                continue;

            std::string label = text->getText().empty() ? ("Text##" + std::to_string(i)) : (text->getText() + "##" + std::to_string(i));
            const bool selected = m_hasSelectedUIElement && m_uiSelectionType == UISelectionType::Text && m_selectedUIElementIndex == i;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                m_hasSelectedUIElement = true;
                m_uiSelectionType = UISelectionType::Text;
                m_selectedUIElementIndex = i;
            }
        }
    }

    if (ImGui::CollapsingHeader("UIButton", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (std::size_t i = 0; i < buttons.size(); ++i)
        {
            const auto &button = buttons[i];
            if (!button)
                continue;

            std::string label = button->getLabel().empty() ? ("Button##" + std::to_string(i)) : (button->getLabel() + "##" + std::to_string(i));
            const bool selected = m_hasSelectedUIElement && m_uiSelectionType == UISelectionType::Button && m_selectedUIElementIndex == i;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                m_hasSelectedUIElement = true;
                m_uiSelectionType = UISelectionType::Button;
                m_selectedUIElementIndex = i;
            }
        }
    }

    if (ImGui::CollapsingHeader("Billboards", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (std::size_t i = 0; i < billboards.size(); ++i)
        {
            const auto &billboard = billboards[i];
            if (!billboard)
                continue;

            std::string label = "Billboard " + std::to_string(i);
            const bool selected = m_hasSelectedUIElement && m_uiSelectionType == UISelectionType::Billboard && m_selectedUIElementIndex == i;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                m_hasSelectedUIElement = true;
                m_uiSelectionType = UISelectionType::Billboard;
                m_selectedUIElementIndex = i;
            }
        }
    }

    if (m_hasSelectedUIElement)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Selected UI Element");

        switch (m_uiSelectionType)
        {
        case UISelectionType::Text:
        {
            auto *text = texts[m_selectedUIElementIndex].get();
            bool enabled = text->isEnabled();
            if (ImGui::Checkbox("Enabled", &enabled))
                text->setEnabled(enabled);

            std::array<char, 512> textBuffer{};
            std::strncpy(textBuffer.data(), text->getText().c_str(), textBuffer.size() - 1);
            if (ImGui::InputText("Text", textBuffer.data(), textBuffer.size()))
                text->setText(std::string(textBuffer.data()));

            glm::vec2 pos = text->getPosition();
            if (ImGui::DragFloat2("Position (NDC)", glm::value_ptr(pos), 0.01f, -1.0f, 1.0f))
                text->setPosition(glm::clamp(pos, glm::vec2(-1.0f), glm::vec2(1.0f)));

            float scale = text->getScale();
            if (ImGui::DragFloat("Scale", &scale, 0.01f, 0.01f, 100.0f))
                text->setScale(scale);

            float rotation = text->getRotation();
            if (ImGui::DragFloat("Rotation (deg)", &rotation, 0.25f, -360.0f, 360.0f))
                text->setRotation(rotation);

            glm::vec4 color = text->getColor();
            if (ImGui::ColorEdit4("Color", glm::value_ptr(color)))
                text->setColor(color);

            static std::array<char, 512> textFontPathBuffer{};
            static std::size_t textFontPathBufferIndex = std::numeric_limits<std::size_t>::max();
            if (textFontPathBufferIndex != m_selectedUIElementIndex)
            {
                std::string currentPath;
                if (const auto *font = text->getFont())
                    currentPath = font->getFontPath();
                if (currentPath.empty())
                    currentPath = "./resources/fonts/JetBrainsMono-Regular.ttf";
                std::strncpy(textFontPathBuffer.data(), currentPath.c_str(), textFontPathBuffer.size() - 1);
                textFontPathBuffer[textFontPathBuffer.size() - 1] = '\0';
                textFontPathBufferIndex = m_selectedUIElementIndex;
            }

            ImGui::InputText("Font Path", textFontPathBuffer.data(), textFontPathBuffer.size());
            if (ImGui::Button("Load Text Font"))
                text->loadFont(std::string(textFontPathBuffer.data()));

            break;
        }
        case UISelectionType::Button:
        {
            auto *button = buttons[m_selectedUIElementIndex].get();
            bool enabled = button->isEnabled();
            if (ImGui::Checkbox("Enabled", &enabled))
                button->setEnabled(enabled);

            glm::vec2 pos = button->getPosition();
            if (ImGui::DragFloat2("Position (NDC)", glm::value_ptr(pos), 0.01f, -1.0f, 1.0f))
                button->setPosition(pos);

            glm::vec2 size = button->getSize();
            if (ImGui::DragFloat2("Size (NDC)", glm::value_ptr(size), 0.01f, 0.01f, 2.0f))
                button->setSize(glm::max(size, glm::vec2(0.01f)));

            glm::vec4 background = button->getBackgroundColor();
            if (ImGui::ColorEdit4("Background", glm::value_ptr(background)))
                button->setBackgroundColor(background);

            glm::vec4 hover = button->getHoverColor();
            if (ImGui::ColorEdit4("Hover", glm::value_ptr(hover)))
                button->setHoverColor(hover);

            glm::vec4 border = button->getBorderColor();
            if (ImGui::ColorEdit4("Border", glm::value_ptr(border)))
                button->setBorderColor(border);

            float borderWidth = button->getBorderWidth();
            if (ImGui::DragFloat("Border Width", &borderWidth, 0.001f, 0.0f, 1.0f))
                button->setBorderWidth(std::max(0.0f, borderWidth));

            std::array<char, 512> labelBuffer{};
            std::strncpy(labelBuffer.data(), button->getLabel().c_str(), labelBuffer.size() - 1);
            if (ImGui::InputText("Label", labelBuffer.data(), labelBuffer.size()))
                button->setLabel(std::string(labelBuffer.data()));

            glm::vec4 labelColor = button->getLabelColor();
            if (ImGui::ColorEdit4("Label Color", glm::value_ptr(labelColor)))
                button->setLabelColor(labelColor);

            float labelScale = button->getLabelScale();
            if (ImGui::DragFloat("Label Scale", &labelScale, 0.01f, 0.01f, 100.0f))
                button->setLabelScale(std::max(0.01f, labelScale));

            float rotation = button->getRotation();
            if (ImGui::DragFloat("Rotation (deg)", &rotation, 0.25f, -360.0f, 360.0f))
                button->setRotation(rotation);

            static std::array<char, 512> buttonFontPathBuffer{};
            static std::size_t buttonFontPathBufferIndex = std::numeric_limits<std::size_t>::max();
            if (buttonFontPathBufferIndex != m_selectedUIElementIndex)
            {
                std::string currentPath;
                if (const auto *font = button->getFont())
                    currentPath = font->getFontPath();
                if (currentPath.empty())
                    currentPath = "./resources/fonts/JetBrainsMono-Regular.ttf";
                std::strncpy(buttonFontPathBuffer.data(), currentPath.c_str(), buttonFontPathBuffer.size() - 1);
                buttonFontPathBuffer[buttonFontPathBuffer.size() - 1] = '\0';
                buttonFontPathBufferIndex = m_selectedUIElementIndex;
            }

            ImGui::InputText("Button Font Path", buttonFontPathBuffer.data(), buttonFontPathBuffer.size());
            if (ImGui::Button("Load Button Font"))
                button->loadFont(std::string(buttonFontPathBuffer.data()));

            break;
        }
        case UISelectionType::Billboard:
        {
            auto *billboard = billboards[m_selectedUIElementIndex].get();
            bool enabled = billboard->isEnabled();
            if (ImGui::Checkbox("Enabled", &enabled))
                billboard->setEnabled(enabled);

            glm::vec3 worldPosition = billboard->getWorldPosition();
            if (ImGui::DragFloat3("World Position", glm::value_ptr(worldPosition), 0.05f))
                billboard->setWorldPosition(worldPosition);

            float size = billboard->getSize();
            if (ImGui::DragFloat("Size", &size, 0.01f, 0.01f, 100.0f))
                billboard->setSize(std::max(0.01f, size));

            float rotation = billboard->getRotation();
            if (ImGui::DragFloat("Rotation (deg)", &rotation, 0.25f, -360.0f, 360.0f))
                billboard->setRotation(rotation);

            glm::vec4 color = billboard->getColor();
            if (ImGui::ColorEdit4("Color", glm::value_ptr(color)))
                billboard->setColor(color);

            static std::array<char, 512> texturePathBuffer{};
            static std::size_t texturePathBufferIndex = std::numeric_limits<std::size_t>::max();
            if (texturePathBufferIndex != m_selectedUIElementIndex)
            {
                const std::string currentPath = billboard->getTexturePath();
                std::strncpy(texturePathBuffer.data(), currentPath.c_str(), texturePathBuffer.size() - 1);
                texturePathBuffer[texturePathBuffer.size() - 1] = '\0';
                texturePathBufferIndex = m_selectedUIElementIndex;
            }

            ImGui::InputText("Texture Path", texturePathBuffer.data(), texturePathBuffer.size());
            if (ImGui::Button("Load Billboard Texture"))
                billboard->loadTexture(std::string(texturePathBuffer.data()));
            ImGui::SameLine();
            if (ImGui::Button("Clear Billboard Texture"))
            {
                billboard->clearTexture();
                texturePathBuffer[0] = '\0';
            }

            break;
        }
        case UISelectionType::None:
        default:
            break;
        }

        ImGui::Separator();
        if (ImGui::Button("Delete Selected UI Element"))
        {
            bool removedElement = false;
            switch (m_uiSelectionType)
            {
            case UISelectionType::Text:
                if (m_selectedUIElementIndex < texts.size())
                {
                    m_scene->removeUIText(texts[m_selectedUIElementIndex].get());
                    removedElement = true;
                }
                break;
            case UISelectionType::Button:
                if (m_selectedUIElementIndex < buttons.size())
                {
                    m_scene->removeUIButton(buttons[m_selectedUIElementIndex].get());
                    removedElement = true;
                }
                break;
            case UISelectionType::Billboard:
                if (m_selectedUIElementIndex < billboards.size())
                {
                    m_scene->removeBillboard(billboards[m_selectedUIElementIndex].get());
                    removedElement = true;
                }
                break;
            case UISelectionType::None:
            default:
                break;
            }

            if (removedElement)
            {
                clearSelectedUIElement();
                captureSceneActionSnapshot("Delete UI element");
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear UI Selection"))
            clearSelectedUIElement();
    }

    ImGui::End();
}

void Editor::drawTerrainTools()
{
    if (!m_showTerrainTools)
        return;

    m_terrainTools.draw(&m_showTerrainTools, &m_notificationManager);
}

void Editor::drawViewport(VkDescriptorSet viewportDescriptorSet)
{
    //! This is the biggest dog shit I've ever seen
    if (m_renderOnlyViewport)
    {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::Begin("##Viewport", nullptr, flags);

        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 size = ImGui::GetContentRegionAvail();

        auto it = m_drawQueue.find("Viewport");
        if (it != m_drawQueue.end())
        {
            for (auto &fn : it->second)
                fn(dl, origin, size);

            it->second.clear();
        }

        ImGui::PopStyleColor();
        ImGui::End();

        return;
    }
    else
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_None);

    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    ImGui::Image(viewportDescriptorSet, ImVec2(viewportPanelSize.x, viewportPanelSize.y));
    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageMax = ImGui::GetItemRectMax();
    const bool imageHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (m_uiPlacementGridEnabled)
    {
        const float imageWidth = imageMax.x - imageMin.x;
        const float imageHeight = imageMax.y - imageMin.y;
        if (imageWidth > 0.0f && imageHeight > 0.0f)
        {
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const float gridStep = std::clamp(m_uiPlacementGridStepNdc, 0.02f, 1.0f);

            const ImU32 shadowColor = IM_COL32(0, 0, 0, 170);
            const ImU32 regularColor = IM_COL32(25, 245, 255, 185);
            const ImU32 axisColor = IM_COL32(255, 135, 40, 230);
            const float axisThreshold = gridStep * 0.5f;

            for (float x = -1.0f; x <= 1.0001f; x += gridStep)
            {
                const glm::vec2 pixel = ndcToViewportPixel(glm::vec2(x, 0.0f), imageMin, imageMax);
                const bool isAxis = std::abs(x) <= axisThreshold;
                const float lineThickness = isAxis ? 1.8f : 1.2f;
                drawList->AddLine(ImVec2(pixel.x, imageMin.y), ImVec2(pixel.x, imageMax.y), shadowColor, lineThickness + 1.2f);
                drawList->AddLine(ImVec2(pixel.x, imageMin.y), ImVec2(pixel.x, imageMax.y), isAxis ? axisColor : regularColor, lineThickness);
            }

            for (float y = -1.0f; y <= 1.0001f; y += gridStep)
            {
                const glm::vec2 pixel = ndcToViewportPixel(glm::vec2(0.0f, y), imageMin, imageMax);
                const bool isAxis = std::abs(y) <= axisThreshold;
                const float lineThickness = isAxis ? 1.8f : 1.2f;
                drawList->AddLine(ImVec2(imageMin.x, pixel.y), ImVec2(imageMax.x, pixel.y), shadowColor, lineThickness + 1.2f);
                drawList->AddLine(ImVec2(imageMin.x, pixel.y), ImVec2(imageMax.x, pixel.y), isAxis ? axisColor : regularColor, lineThickness);
            }

            if (imageHovered && m_uiPlacementTool != UIPlacementTool::None)
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                glm::vec2 snappedNdc = viewportPixelToNdc(mouse, imageMin, imageMax);
                snappedNdc = snapNdcToGrid(snappedNdc);
                const glm::vec2 snappedPixel = ndcToViewportPixel(snappedNdc, imageMin, imageMax);
                drawList->AddCircleFilled(ImVec2(snappedPixel.x, snappedPixel.y), 4.0f, IM_COL32(255, 210, 90, 220));
                drawList->AddCircle(ImVec2(snappedPixel.x, snappedPixel.y), 8.0f, IM_COL32(255, 210, 90, 160), 16, 1.5f);
            }
        }
    }

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

        if (ImGui::MenuItem("Default Character"))
        {
            addDefaultCharacterEntity("Character");
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

    bool terrainBrushConsumed = false;
    const bool canUseTerrainBrush = hovered &&
                                    imageHovered &&
                                    m_showTerrainTools &&
                                    !m_isViewportMouseCaptured &&
                                    !ImGuizmo::IsOver() &&
                                    !m_isColliderHandleHovered &&
                                    !m_isColliderHandleActive &&
                                    m_uiPlacementTool == UIPlacementTool::None &&
                                    m_currentMode == EditorMode::EDIT &&
                                    static_cast<bool>(m_editorCamera);

    if (canUseTerrainBrush)
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool mouseInsideImage = mouse.x >= imageMin.x && mouse.x < imageMax.x &&
                                      mouse.y >= imageMin.y && mouse.y < imageMax.y;

        if (mouseInsideImage && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const glm::vec2 ndcPosition = viewportPixelToNdc(mouse, imageMin, imageMax);
            terrainBrushConsumed = m_terrainTools.applyBrushStrokeFromNdc(
                ndcPosition,
                m_editorCamera.get(),
                m_selectedEntity,
                io.DeltaTime,
                ImGui::IsMouseClicked(ImGuiMouseButton_Left));

            if (terrainBrushConsumed)
                m_hasPendingObjectPick = false;
        }
        else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_terrainTools.cancelBrushStroke();
    }
    else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        m_terrainTools.cancelBrushStroke();

    if (viewportFocused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        if (m_currentMode != EditorMode::EDIT)
        {
            m_notificationManager.showError("Cannot save scene while playing. Stop Play first.");
            VX_EDITOR_WARNING_STREAM("Blocked Ctrl+S while not in Edit mode.\n");
        }

        auto project = m_currentProject.lock();
        if (m_scene && project && m_currentMode == EditorMode::EDIT)
        {
            m_scene->saveSceneToFile(project->entryScene);
            m_notificationManager.showInfo("Scene saved");
            VX_EDITOR_INFO_STREAM("Scene saved to: " << project->entryScene);
        }
    }

    auto &window = core::VulkanContext::getContext()->getSwapchain()->getWindow();
    GLFWwindow *windowHandler = window.getRawHandler();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() &&
        !terrainBrushConsumed &&
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
            if (!placeUIElementAtViewportPosition(mouse, imageMin, imageMax))
            {
                const float u = std::clamp((mouse.x - imageMin.x) / imageWidth, 0.0f, 0.999999f);
                const float v = std::clamp((mouse.y - imageMin.y) / imageHeight, 0.0f, 0.999999f);

                m_pendingPickX = static_cast<uint32_t>(u * static_cast<float>(m_viewportSizeX));
                m_pendingPickY = static_cast<uint32_t>(v * static_cast<float>(m_viewportSizeY));
                m_hasPendingObjectPick = true;
            }
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

void Editor::drawGameViewport(VkDescriptorSet viewportDescriptorSet, bool hasGameCamera)
{
    const bool gameViewportWindowVisible = ImGui::Begin("Game Viewport", nullptr, ImGuiWindowFlags_None);
    m_isGameViewportVisible = gameViewportWindowVisible && !ImGui::IsWindowCollapsed();

    const ImVec2 viewportPanelSize = gameViewportWindowVisible ? ImGui::GetContentRegionAvail() : ImVec2(0.0f, 0.0f);
    const uint32_t x = static_cast<uint32_t>(std::max(viewportPanelSize.x, 0.0f));
    const uint32_t y = static_cast<uint32_t>(std::max(viewportPanelSize.y, 0.0f));

    const bool sizeChanged = (m_gameViewportSizeX != x || m_gameViewportSizeY != y);
    if (sizeChanged)
    {
        m_gameViewportSizeX = x;
        m_gameViewportSizeY = y;

        for (const auto &function : m_onGameViewportWindowResized)
            if (function)
                function(m_gameViewportSizeX, m_gameViewportSizeY);
    }

    if (gameViewportWindowVisible && viewportDescriptorSet != VK_NULL_HANDLE && hasGameCamera)
    {
        ImGui::Image(viewportDescriptorSet, ImVec2(viewportPanelSize.x, viewportPanelSize.y));
    }
    else if (gameViewportWindowVisible)
    {
        const char *statusText = hasGameCamera ? "Game viewport is unavailable." : "No camera in scene";
        const ImVec2 textSize = ImGui::CalcTextSize(statusText);
        const float textPosX = std::max((viewportPanelSize.x - textSize.x) * 0.5f, 0.0f);
        const float textPosY = std::max((viewportPanelSize.y - textSize.y) * 0.5f, 0.0f);
        ImGui::SetCursorPos(ImVec2(textPosX, textPosY));
        ImGui::TextDisabled("%s", statusText);
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
