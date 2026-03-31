#include "Editor/Panels/MaterialEditor.hpp"

#include "Editor/AssetsPreviewSystem.hpp"

#include "Engine/Shaders/ShaderCompiler.hpp"
#include "Engine/Assets/Asset.hpp"
#include "Engine/Assets/AssetsSerializer.hpp"
#include "Engine/Assets/AssetsLoader.hpp"

#include "imgui.h"
#include "imgui_node_editor.h"
#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>

namespace ed = ax::NodeEditor;

ELIX_NESTED_NAMESPACE_BEGIN(editor)

namespace
{
    std::string toLowerCopy(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return text;
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
        std::error_code directoryError;
        const bool canScanDirectory = !projectRoot.empty() && std::filesystem::is_directory(projectRoot, directoryError) && !directoryError;

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
        for (std::filesystem::recursive_directory_iterator iterator(
                 projectRoot,
                 std::filesystem::directory_options::skip_permission_denied,
                 scanError);
             canScanDirectory && !scanError && iterator != std::filesystem::recursive_directory_iterator();
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

    std::string toGLSLFloat(float value)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.6f", value);
        return buffer;
    }

    const char *noiseTargetSlots[] = {"albedo", "emissive", "roughness", "metallic", "ao", "alpha"};
    const char *noiseTargetDisplay[] = {"Base Color", "Emissive", "Roughness", "Metallic", "AO", "Alpha"};
    const char *colorTargetSlots[] = {"albedo", "emissive"};
    const char *colorTargetDisplay[] = {"Base Color", "Emissive"};
    const char *blendModeLabels[] = {"Replace", "Multiply", "Add"};

    bool isColorTargetSlot(const std::string &targetSlot)
    {
        return targetSlot == "albedo" || targetSlot == "emissive";
    }

    int findTargetSlotIndex(const std::string &targetSlot, const char *const *slots, int count)
    {
        for (int index = 0; index < count; ++index)
        {
            if (targetSlot == slots[index])
                return index;
        }

        return 0;
    }

    int findBlendModeIndex(engine::CPUMaterial::NoiseNodeParams::BlendMode blendMode)
    {
        switch (blendMode)
        {
        case engine::CPUMaterial::NoiseNodeParams::BlendMode::Replace:
            return 0;
        case engine::CPUMaterial::NoiseNodeParams::BlendMode::Multiply:
            return 1;
        case engine::CPUMaterial::NoiseNodeParams::BlendMode::Add:
            return 2;
        }

        return 0;
    }

    int findBlendModeIndex(engine::CPUMaterial::ColorNodeParams::BlendMode blendMode)
    {
        switch (blendMode)
        {
        case engine::CPUMaterial::ColorNodeParams::BlendMode::Replace:
            return 0;
        case engine::CPUMaterial::ColorNodeParams::BlendMode::Multiply:
            return 1;
        case engine::CPUMaterial::ColorNodeParams::BlendMode::Add:
            return 2;
        }

        return 0;
    }

    engine::CPUMaterial::NoiseNodeParams::BlendMode noiseBlendModeFromIndex(int index)
    {
        switch (index)
        {
        case 1:
            return engine::CPUMaterial::NoiseNodeParams::BlendMode::Multiply;
        case 2:
            return engine::CPUMaterial::NoiseNodeParams::BlendMode::Add;
        default:
            return engine::CPUMaterial::NoiseNodeParams::BlendMode::Replace;
        }
    }

    engine::CPUMaterial::ColorNodeParams::BlendMode colorBlendModeFromIndex(int index)
    {
        switch (index)
        {
        case 0:
            return engine::CPUMaterial::ColorNodeParams::BlendMode::Replace;
        case 2:
            return engine::CPUMaterial::ColorNodeParams::BlendMode::Add;
        default:
            return engine::CPUMaterial::ColorNodeParams::BlendMode::Multiply;
        }
    }

    void appendScalarBlendGLSL(std::string &out,
                               const std::string &targetVariable,
                               const std::string &valueExpression,
                               engine::CPUMaterial::NoiseNodeParams::BlendMode blendMode,
                               float minValue,
                               float maxValue)
    {
        const std::string minString = toGLSLFloat(minValue);
        const std::string maxString = toGLSLFloat(maxValue);
        const std::string clampExpr = "clamp(" + valueExpression + ", " + minString + ", " + maxString + ")";

        switch (blendMode)
        {
        case engine::CPUMaterial::NoiseNodeParams::BlendMode::Replace:
            out += targetVariable + " = " + clampExpr + ";\n";
            break;
        case engine::CPUMaterial::NoiseNodeParams::BlendMode::Multiply:
            out += targetVariable + " = clamp(" + targetVariable + " * " + clampExpr + ", " + minString + ", " + maxString + ");\n";
            break;
        case engine::CPUMaterial::NoiseNodeParams::BlendMode::Add:
            out += targetVariable + " = clamp(" + targetVariable + " + " + clampExpr + ", " + minString + ", " + maxString + ");\n";
            break;
        }
    }

    void appendColorBlendGLSL(std::string &out,
                              const std::string &targetVariable,
                              const std::string &valueExpression,
                              engine::CPUMaterial::ColorNodeParams::BlendMode blendMode,
                              bool hdrColor)
    {
        const std::string minExpr = hdrColor ? "vec3(0.0)" : "vec3(0.0)";
        const std::string clampExpr = hdrColor
                                          ? "max(" + valueExpression + ", " + minExpr + ")"
                                          : "clamp(" + valueExpression + ", vec3(0.0), vec3(1.0))";

        switch (blendMode)
        {
        case engine::CPUMaterial::ColorNodeParams::BlendMode::Replace:
            out += targetVariable + " = " + clampExpr + ";\n";
            break;
        case engine::CPUMaterial::ColorNodeParams::BlendMode::Multiply:
            out += targetVariable + " = " + (hdrColor ? "max(" : "clamp(") + targetVariable + " * " + clampExpr +
                   (hdrColor ? ", vec3(0.0));\n" : ", vec3(0.0), vec3(1.0));\n");
            break;
        case engine::CPUMaterial::ColorNodeParams::BlendMode::Add:
            out += targetVariable + " = " + (hdrColor ? "max(" : "clamp(") + targetVariable + " + " + clampExpr +
                   (hdrColor ? ", vec3(0.0));\n" : ", vec3(0.0), vec3(1.0));\n");
            break;
        }
    }

    void appendNoiseColorBlendGLSL(std::string &out,
                                   const std::string &targetVariable,
                                   const std::string &valueExpression,
                                   engine::CPUMaterial::NoiseNodeParams::BlendMode blendMode,
                                   bool hdrColor)
    {
        const std::string clampExpr = hdrColor
                                          ? "max(" + valueExpression + ", vec3(0.0))"
                                          : "clamp(" + valueExpression + ", vec3(0.0), vec3(1.0))";

        switch (blendMode)
        {
        case engine::CPUMaterial::NoiseNodeParams::BlendMode::Replace:
            out += targetVariable + " = " + clampExpr + ";\n";
            break;
        case engine::CPUMaterial::NoiseNodeParams::BlendMode::Multiply:
            out += targetVariable + " = " + (hdrColor ? "max(" : "clamp(") + targetVariable + " * " + clampExpr +
                   (hdrColor ? ", vec3(0.0));\n" : ", vec3(0.0), vec3(1.0));\n");
            break;
        case engine::CPUMaterial::NoiseNodeParams::BlendMode::Add:
            out += targetVariable + " = " + (hdrColor ? "max(" : "clamp(") + targetVariable + " + " + clampExpr +
                   (hdrColor ? ", vec3(0.0));\n" : ", vec3(0.0), vec3(1.0));\n");
            break;
        }
    }

    std::string generateColorGLSL(const std::vector<engine::CPUMaterial::ColorNodeParams> &nodes)
    {
        std::string out = "// [COLOR NODES - auto-generated]\n";
        bool hasAnyActiveNode = false;

        for (size_t index = 0; index < nodes.size(); ++index)
        {
            const auto &node = nodes[index];
            if (!node.active)
                continue;

            hasAnyActiveNode = true;
            const std::string valueExpression =
                "vec3(" + toGLSLFloat(node.color.r) + ", " +
                toGLSLFloat(node.color.g) + ", " +
                toGLSLFloat(node.color.b) + ") * " + toGLSLFloat(node.strength);

            if (node.targetSlot == "emissive")
                appendColorBlendGLSL(out, "emissive", valueExpression, node.blendMode, true);
            else
                appendColorBlendGLSL(out, "albedo", valueExpression, node.blendMode, false);

            out += '\n';
        }

        return hasAnyActiveNode ? out : std::string{};
    }

    std::string generateNoiseGLSL(const std::vector<engine::CPUMaterial::NoiseNodeParams> &nodes)
    {
        std::string out = "// [NOISE NODES - auto-generated]\n";
        bool hasAnyActiveNode = false;
        bool usesWorldSpace = false;

        for (const auto &node : nodes)
        {
            if (!node.active)
                continue;

            hasAnyActiveNode = true;
            usesWorldSpace = usesWorldSpace || node.worldSpace;
        }

        if (!hasAnyActiveNode)
            return {};

        if (usesWorldSpace)
            out += "vec3 _elixWorldPos = (cameraUniformObject.invView * vec4(fragPositionView, 1.0)).xyz;\n\n";

        size_t generatedIndex = 0;
        for (const auto &node : nodes)
        {
            if (!node.active)
                continue;

            const std::string valueVariable = "_nv" + std::to_string(generatedIndex++);
            const std::string scaleValue = toGLSLFloat(node.scale);
            const std::string persistenceValue = toGLSLFloat(node.persistence);
            const std::string lacunarityValue = toGLSLFloat(node.lacunarity);

            switch (node.type)
            {
            case engine::CPUMaterial::NoiseNodeParams::Type::Value:
                if (node.worldSpace)
                    out += "float " + valueVariable + " = elixValueNoise3D(_elixWorldPos * " + scaleValue + ");\n";
                else
                    out += "float " + valueVariable + " = elixValueNoise2D(uv * " + scaleValue + ");\n";
                break;
            case engine::CPUMaterial::NoiseNodeParams::Type::Gradient:
                if (node.worldSpace)
                    out += "float " + valueVariable + " = elixGradientNoise2D(_elixWorldPos.xz * " + scaleValue + ");\n";
                else
                    out += "float " + valueVariable + " = elixGradientNoise2D(uv * " + scaleValue + ");\n";
                break;
            case engine::CPUMaterial::NoiseNodeParams::Type::FBM:
                if (node.worldSpace)
                    out += "float " + valueVariable + " = elixFbm3D(_elixWorldPos * " + scaleValue + ", " +
                           std::to_string(node.octaves) + ", " + persistenceValue + ", " + lacunarityValue + ");\n";
                else
                    out += "float " + valueVariable + " = elixFbm2D(uv * " + scaleValue + ", " +
                           std::to_string(node.octaves) + ", " + persistenceValue + ", " + lacunarityValue + ");\n";
                break;
            case engine::CPUMaterial::NoiseNodeParams::Type::Voronoi:
                if (node.worldSpace)
                    out += "float " + valueVariable + " = elixVoronoiNoise2D(_elixWorldPos.xz * " + scaleValue + ");\n";
                else
                    out += "float " + valueVariable + " = elixVoronoiNoise2D(uv * " + scaleValue + ");\n";
                break;
            }

            if (node.targetSlot == "roughness")
                appendScalarBlendGLSL(out, "roughness", valueVariable, node.blendMode, 0.04f, 1.0f);
            else if (node.targetSlot == "metallic")
                appendScalarBlendGLSL(out, "metallic", valueVariable, node.blendMode, 0.0f, 1.0f);
            else if (node.targetSlot == "ao")
                appendScalarBlendGLSL(out, "ao", valueVariable, node.blendMode, 0.0f, 1.0f);
            else if (node.targetSlot == "alpha")
                appendScalarBlendGLSL(out, "alpha", valueVariable, node.blendMode, 0.0f, 1.0f);
            else if (isColorTargetSlot(node.targetSlot))
            {
                const std::string colorVariable = "_nvc" + std::to_string(generatedIndex);
                out += "vec3 " + colorVariable + " = mix(vec3(" +
                       toGLSLFloat(node.rampColorA.r) + ", " + toGLSLFloat(node.rampColorA.g) + ", " + toGLSLFloat(node.rampColorA.b) +
                       "), vec3(" +
                       toGLSLFloat(node.rampColorB.r) + ", " + toGLSLFloat(node.rampColorB.g) + ", " + toGLSLFloat(node.rampColorB.b) +
                       "), " + valueVariable + ");\n";

                if (node.targetSlot == "emissive")
                    appendNoiseColorBlendGLSL(out, "emissive", colorVariable, node.blendMode, true);
                else
                    appendNoiseColorBlendGLSL(out, "albedo", colorVariable, node.blendMode, false);
            }

            out += '\n';
        }

        return out;
    }

}

MaterialEditor::MaterialEditor()
{
}

MaterialEditor::~MaterialEditor()
{
    for (auto &[materialPath, state] : m_materialEditorUiState)
    {
        (void)materialPath;
        if (!state.nodeEditorContext)
            continue;

        ed::DestroyEditor(state.nodeEditorContext);
        state.nodeEditorContext = nullptr;
        state.nodeEditorInitialized = false;
    }
}

void MaterialEditor::setProject(Project *project)
{
    if (m_project != project)
        closeCurrentMaterialEditor();

    m_project = project;
}

void MaterialEditor::notify(NotificationType type, const std::string &message)
{
    if (!m_notificationManager)
        return;

    m_notificationManager->show(message, type);
}

void MaterialEditor::setNotificationManager(NotificationManager *notificationManager)
{
    m_notificationManager = notificationManager;
}

void MaterialEditor::setCenterDockId(ImGuiID dockId)
{
    m_centerDockId = dockId;
}

void MaterialEditor::setAssetsPreviewSystem(AssetsPreviewSystem *assetsPreviewSystem)
{
    m_assetsPreviewSystem = assetsPreviewSystem;
}

void MaterialEditor::destroyNodeEditorStateForPath(const std::filesystem::path &materialPath)
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
}

void MaterialEditor::closeCurrentMaterialEditor()
{
    destroyNodeEditorStateForPath(m_currentOpenedMaterialEditor.path);
    m_currentOpenedMaterialEditor = {};
}

void MaterialEditor::openMaterialEditor(const std::filesystem::path &path)
{
    std::filesystem::path normalizedPath = path;

    if (m_project)
        normalizedPath = resolveMaterialPathAgainstProjectRoot(path.string(), resolveProjectRootPath(*m_project));

    if (m_currentOpenedMaterialEditor.path == normalizedPath)
    {
        OpenMaterialEditor editor;
        editor.path = normalizedPath;
        editor.open = true;
        editor.dirty = m_currentOpenedMaterialEditor.dirty;
        m_currentOpenedMaterialEditor = editor;
        return;
    }

    closeCurrentMaterialEditor();

    OpenMaterialEditor editor;
    editor.path = normalizedPath;
    editor.open = true;
    editor.dirty = false;
    m_currentOpenedMaterialEditor = editor;
}

void MaterialEditor::closeMaterialEditor(const std::filesystem::path &path)
{
    std::filesystem::path normalizedPath = path;
    if (m_project)
        normalizedPath = resolveMaterialPathAgainstProjectRoot(path.string(), resolveProjectRootPath(*m_project));
    else
        normalizedPath = path.lexically_normal();

    if (m_currentOpenedMaterialEditor.path != normalizedPath)
        return;

    closeCurrentMaterialEditor();
}

void MaterialEditor::setSaveMaterialToDiskFunction(const std::function<bool(const std::filesystem::path &path, const engine::CPUMaterial &cpuMaterial)> &function)
{
    m_saveMaterialToDisk = function;
}

void MaterialEditor::setReloadMaterialFromDiskFunction(const std::function<bool(const std::filesystem::path &path)> &function)
{
    m_reloadMaterialFromDisk = function;
}

void MaterialEditor::setEnsureProjectTextureLoadedFunction(const std::function<engine::Texture::SharedPtr(const std::string &texturePath, TextureUsage usage)> &function)
{
    m_ensureProjectTextureLoaded = function;
}

void MaterialEditor::setEnsureProjectTextureLoadedPreviewFunction(const std::function<engine::Texture::SharedPtr(const std::string &texturePath, TextureUsage usage)> &function)
{
    m_ensureProjectTextureLoadedPreview = function;
}

bool MaterialEditor::reloadMaterialFromDisk(const std::filesystem::path &path)
{
    if (m_reloadMaterialFromDisk)
        return m_reloadMaterialFromDisk(path);

    //! Maybe this is deadlock
    return false;
}

bool MaterialEditor::saveMaterialToDisk(const std::filesystem::path &path, const engine::CPUMaterial &cpuMaterial)
{
    if (m_saveMaterialToDisk)
        return m_saveMaterialToDisk(path, cpuMaterial);

    //! Maybe this is deadlock
    return false;
}

engine::Texture::SharedPtr MaterialEditor::ensureProjectTextureLoaded(const std::string &texturePath, TextureUsage usage)
{
    if (m_ensureProjectTextureLoaded)
        return m_ensureProjectTextureLoaded(texturePath, usage);

    return nullptr;
}

engine::Texture::SharedPtr MaterialEditor::ensureProjectTextureLoadedPreview(const std::string &texturePath, TextureUsage usage)
{
    if (m_ensureProjectTextureLoadedPreview)
        return m_ensureProjectTextureLoadedPreview(texturePath, usage);

    return nullptr;
}

void MaterialEditor::draw()
{
    if (!m_project || m_currentOpenedMaterialEditor.path.empty())
        return;

    const std::filesystem::path projectRoot = resolveProjectRootPath(*m_project);

    auto &matEditor = m_currentOpenedMaterialEditor;
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

    if (ImGui::Begin(windowName.c_str(), &keepOpen))
    {
        auto materialRecordIt = m_project->cache.materialsByPath.find(normalizedMatPath);
        if (materialRecordIt == m_project->cache.materialsByPath.end())
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

            materialRecordIt = m_project->cache.materialsByPath.find(normalizedMatPath);
            if (!loaded || materialRecordIt == m_project->cache.materialsByPath.end())
            {
                ImGui::TextDisabled("Failed to load material.");
                ImGui::TextWrapped("%s", matEditor.path.string().c_str());
                ImGui::Spacing();

                if (ImGui::Button("Initialize Default Material"))
                {
                    engine::CPUMaterial defaultMaterial{};
                    defaultMaterial.name = matEditor.path.stem().string();

                    if (saveMaterialToDisk(matEditor.path, defaultMaterial) && reloadMaterialFromDisk(matEditor.path))
                        notify(NotificationType::Success, "Material initialized");
                    else
                        notify(NotificationType::Error, "Failed to initialize material");
                }

                // ImGui::SameLine();
                // if (ImGui::Button("Open As Text"))
                //     openTextDocument(matEditor.path);

                ImGui::End();
                matEditor.open = keepOpen;
                closeRequested = !matEditor.open;
                if (!closeRequested)
                    return;

                closeCurrentMaterialEditor();
                return;
            }
        }
        if (materialRecordIt != m_project->cache.materialsByPath.end())
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
                    return;

                closeCurrentMaterialEditor();
                return;
            }
            else
            {
                if (!ui.nodeEditorInitialized)
                {
                    ed::Config config;
                    config.SettingsFile = nullptr;
                    ui.nodeEditorContext = ed::CreateEditor(&config);
                    ui.nodeEditorInitialized = true;

                    if (ui.nodeEditorContext)
                    {
                        ed::SetCurrentEditor(ui.nodeEditorContext);
                        ed::SetNodePosition(ed::NodeId(ui.mappingNodeId), ImVec2(20.0f, 130.0f));
                        ed::SetNodePosition(ed::NodeId(ui.texturesNodeId), ImVec2(420.0f, 70.0f));
                        ed::SetNodePosition(ed::NodeId(ui.outputNodeId), ImVec2(980.0f, 90.0f));
                        ed::SetCurrentEditor(nullptr);
                    }
                }

                const bool isMaterialEditorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                const bool isCtrlDown = ImGui::GetIO().KeyCtrl;

                auto setTexturePathAndGpu = [&](std::string &cpuPath,
                                                TextureUsage usage,
                                                const std::string &newPath,
                                                const std::function<void(engine::Texture::SharedPtr)> &assignTexture)
                {
                    cpuPath = newPath;
                    if (newPath.empty())
                    {
                        assignTexture(nullptr);
                    }
                    else
                    {
                        assignTexture(ensureProjectTextureLoaded(newPath, usage));
                    }
                };

                auto saveCurrentMaterial = [&]()
                {
                    cpuMat.noiseNodes.clear();
                    cpuMat.colorNodes.clear();
                    for (const auto &noiseNode : ui.dynamicNoiseNodes)
                        cpuMat.noiseNodes.push_back(noiseNode.params);
                    for (const auto &colorNode : ui.dynamicColorNodes)
                        cpuMat.colorNodes.push_back(colorNode.params);

                    if (saveMaterialToDisk(matEditor.path, cpuMat))
                    {
                        matEditor.dirty = false;
                        notify(NotificationType::Success, "Material saved");
                        VX_EDITOR_INFO_STREAM("Material saved: " << normalizedMatPath);
                    }
                    else
                    {
                        notify(NotificationType::Error, "Failed to save material");
                        VX_EDITOR_ERROR_STREAM("Failed to save material: " << normalizedMatPath);
                    }
                };

                // Compile the current custom expression + noise nodes into a cached .spv.
                auto compileAndApply = [&](bool silent = false) -> bool
                {
                    cpuMat.noiseNodes.clear();
                    cpuMat.colorNodes.clear();
                    for (const auto &noiseNode : ui.dynamicNoiseNodes)
                        cpuMat.noiseNodes.push_back(noiseNode.params);
                    for (const auto &colorNode : ui.dynamicColorNodes)
                        cpuMat.colorNodes.push_back(colorNode.params);

                    const std::string colorPreamble = generateColorGLSL(cpuMat.colorNodes);
                    const std::string noisePreamble = generateNoiseGLSL(cpuMat.noiseNodes);
                    const std::string functions = ui.customFunctionsEditor.GetText();
                    const std::string handExpr  = ui.customExpressionEditor.GetText();

                    // If nothing to compile, clear the custom shader
                    const bool hasColor = !colorPreamble.empty();
                    const bool hasNoise = !noisePreamble.empty();
                    const bool hasExpr  = !handExpr.empty() && handExpr != "\n";
                    if (!hasColor && !hasNoise && !hasExpr && functions.empty())
                    {
                        cpuMat.customExpression.clear();
                        cpuMat.customShaderHash.clear();
                        gpuMat->setCustomFragPath("");
                        ui.customShaderHasError = false;
                        ui.customShaderLastError.clear();
                        ui.initialCustomShaderRefreshDone = true;
                        matEditor.dirty = true;
                        saveCurrentMaterial();
                        return true;
                    }

                    std::string effectiveExpr;
                    if (hasColor)
                        effectiveExpr += colorPreamble + "\n";
                    if (hasNoise)
                        effectiveExpr += noisePreamble + "\n";
                    effectiveExpr += handExpr;

                    std::ifstream templateFile("./resources/shaders/gbuffer_static_template.frag_template");
                    if (!templateFile.is_open())
                    {
                        ui.customShaderLastError = "Template not found: resources/shaders/gbuffer_static_template.frag_template";
                        ui.customShaderHasError = true;
                        return false;
                    }

                    std::string templateSrc((std::istreambuf_iterator<char>(templateFile)),
                                            std::istreambuf_iterator<char>());

                    auto replaceFirst = [](std::string &s, const std::string &from, const std::string &to)
                    {
                        const size_t pos = s.find(from);
                        if (pos != std::string::npos)
                            s.replace(pos, from.size(), to);
                    };

                    replaceFirst(templateSrc, "// <<ELIX_CUSTOM_FUNCTIONS>>", functions);
                    replaceFirst(templateSrc, "// <<ELIX_CUSTOM_EXPRESSION>>", effectiveExpr);

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
                        return false;
                    }

                    if (spv.empty())
                        return false;

                    const std::string hashInput = std::string("gbuffer_bindless_layout_v1\n") + functions + "\n" + effectiveExpr;
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
                        return false;
                    }
                    spvFile.write(reinterpret_cast<const char *>(spv.data()), spv.size() * sizeof(uint32_t));
                    spvFile.close();

                    // Store hand-written part only (noise comes from noiseNodes on next compile)
                    cpuMat.customExpression = "// [FUNCTIONS]\n" + functions + "// [EXPRESSION]\n" + handExpr;
                    cpuMat.customShaderHash = hashStr;
                    gpuMat->setCustomFragPath(spvPath);
                    ui.initialCustomShaderRefreshDone = true;
                    matEditor.dirty = true;
                    saveCurrentMaterial();
                    if (!silent)
                        notify(NotificationType::Success, "Custom shader compiled and applied");
                    return true;
                };

                if (ImGui::Button("Save"))
                    saveCurrentMaterial();
                ImGui::SameLine();

                if (ImGui::Button("Revert"))
                {
                    if (reloadMaterialFromDisk(matEditor.path))
                    {
                        auto refreshedIt = m_project->cache.materialsByPath.find(normalizedMatPath);
                        if (refreshedIt != m_project->cache.materialsByPath.end())
                        {
                            gpuMat = refreshedIt->second.gpu;
                            cpuMat = refreshedIt->second.cpuData;
                            ui.dynamicColorNodes.clear();
                            ui.dynamicNoiseNodes.clear();
                            ui.colorNodesInitialized = false;
                            ui.noiseNodesInitialized = false;
                            ui.initialCustomShaderRefreshDone = false;
                        }

                        matEditor.dirty = false;
                        notify(NotificationType::Info, "Material reloaded from disk");
                        VX_EDITOR_INFO_STREAM("Material reloaded from disk: " << normalizedMatPath);
                    }
                    else
                    {
                        notify(NotificationType::Error, "Failed to reload material from disk");
                        VX_EDITOR_ERROR_STREAM("Failed to reload material from disk: " << normalizedMatPath);
                    }
                }

                ImGui::SameLine();
                ImGui::TextDisabled("%s", matPath.c_str());

                if (isMaterialEditorFocused && isCtrlDown && ImGui::IsKeyPressed(ImGuiKey_S, false))
                    saveCurrentMaterial();

                ImGui::Separator();

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
                            initExpr = src.substr(ePos + exprMarker.size());
                        }
                        else
                        {
                            initExpr = src;
                        }
                    }
                    ui.customFunctionsEditor.SetText(initFuncs);
                    ui.customExpressionEditor.SetText(initExpr);
                    ui.customShaderInitialized = true;

                    if (!ui.colorNodesInitialized)
                    {
                        ui.dynamicColorNodes.clear();
                        for (const auto &savedNode : cpuMat.colorNodes)
                        {
                            MaterialEditorUIState::DynamicColorNode colorNode;
                            colorNode.nodeId = ui.nextDynamicColorNodeId++;
                            colorNode.outputPinId = ui.nextDynamicColorPinId++;
                            colorNode.linkId = ui.nextDynamicColorLinkId++;
                            colorNode.params = savedNode;
                            colorNode.spawnPosition = glm::vec2(700.0f + static_cast<float>(ui.dynamicColorNodes.size()) * 220.0f, 360.0f);
                            colorNode.pendingPlacement = true;
                            ui.dynamicColorNodes.push_back(colorNode);
                        }
                        ui.colorNodesInitialized = true;
                    }

                    if (!ui.noiseNodesInitialized)
                    {
                        ui.dynamicNoiseNodes.clear();
                        for (const auto &savedNode : cpuMat.noiseNodes)
                        {
                            MaterialEditorUIState::DynamicNoiseNode nn;
                            nn.nodeId = ui.nextNoiseNodeId++;
                            nn.outputPinId = ui.nextNoisePinId++;
                            nn.linkId = ui.nextNoiseLinkId++;
                            nn.params = savedNode;
                            nn.spawnPosition = glm::vec2(200.0f + static_cast<float>(ui.dynamicNoiseNodes.size()) * 220.0f, 480.0f);
                            nn.pendingPlacement = true;
                            ui.dynamicNoiseNodes.push_back(nn);
                        }
                        ui.noiseNodesInitialized = true;
                    }

                    const bool hasProceduralGraph =
                        !cpuMat.customExpression.empty() ||
                        !cpuMat.colorNodes.empty() ||
                        !cpuMat.noiseNodes.empty();
                    if (hasProceduralGraph && !ui.initialCustomShaderRefreshDone)
                        compileAndApply(true);
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
                        compileAndApply();

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
                        return;

                    closeCurrentMaterialEditor();
                    return;
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
                    int outputInputPinId;
                    int linkId;
                    std::function<void(engine::Texture::SharedPtr)> assignTexture;
                };

                TextureSlotRow textureRows[] = {
                    {"Base Color", TextureUsage::Color, &cpuMat.albedoTexture, ui.texturesOutAlbedoPinId, ui.outputInAlbedoPinId, ui.linkAlbedoId, [&](engine::Texture::SharedPtr texture)
                     { gpuMat->setAlbedoTexture(texture); }},
                    {"Normal", TextureUsage::Data, &cpuMat.normalTexture, ui.texturesOutNormalPinId, ui.outputInNormalPinId, ui.linkNormalId, [&](engine::Texture::SharedPtr texture)
                     { gpuMat->setNormalTexture(texture); }},
                    {"Roughness/AO (ORM)", TextureUsage::Data, &cpuMat.ormTexture, ui.texturesOutOrmPinId, ui.outputInOrmPinId, ui.linkOrmId, [&](engine::Texture::SharedPtr texture)
                     { gpuMat->setOrmTexture(texture); }},
                    {"Emissive", TextureUsage::Color, &cpuMat.emissiveTexture, ui.texturesOutEmissivePinId, ui.outputInEmissivePinId, ui.linkEmissiveId, [&](engine::Texture::SharedPtr texture)
                     { gpuMat->setEmissiveTexture(texture); }},
                };
                auto applyTextureSlotChange = [&](TextureSlotRow &row, const std::string &newPath)
                {
                    setTexturePathAndGpu(*row.cpuPath, row.usage, newPath, row.assignTexture);
                };
                struct DeferredTexturePopup
                {
                    TextureSlotRow *row{nullptr};
                    std::string popupName;
                };
                std::vector<DeferredTexturePopup> deferredTexturePopups;
                deferredTexturePopups.reserve(std::size(textureRows));

                enum class DeferredEnumPopupKind
                {
                    ColorTarget,
                    ColorBlend,
                    NoiseType,
                    NoiseTarget,
                    NoiseBlend
                };

                struct DeferredEnumPopup
                {
                    DeferredEnumPopupKind kind;
                    int nodeId{0};
                    std::string popupName;
                };

                std::vector<DeferredEnumPopup> deferredEnumPopups;
                deferredEnumPopups.reserve(ui.dynamicColorNodes.size() * 2 + ui.dynamicNoiseNodes.size() * 3);
                std::string deferredEnumPopupToOpen;

                auto drawDeferredEnumSelector = [&](const char *label,
                                                    const char *previewValue,
                                                    DeferredEnumPopupKind kind,
                                                    int nodeId,
                                                    float width)
                {
                    const std::string popupName = "MaterialEnumPopup##" + normalizedMatPath + "_" + std::to_string(nodeId) + "_" + label;
                    deferredEnumPopups.push_back({kind, nodeId, popupName});

                    std::string buttonLabel = std::string(previewValue) + " v##" + popupName;
                    if (ImGui::Button(buttonLabel.c_str(), ImVec2(width, 0.0f)))
                        deferredEnumPopupToOpen = popupName;
                    ImGui::SameLine();
                    ImGui::TextUnformatted(label);
                };

                auto outputPinForTarget = [&](const std::string &targetSlot) -> int
                {
                    if (targetSlot == "normal")
                        return ui.outputInNormalPinId;
                    if (targetSlot == "orm")
                        return ui.outputInOrmPinId;
                    if (targetSlot == "emissive")
                        return ui.outputInEmissivePinId;
                    if (targetSlot == "roughness")
                        return ui.outputInRoughnessPinId;
                    if (targetSlot == "metallic")
                        return ui.outputInMetallicPinId;
                    if (targetSlot == "ao")
                        return ui.outputInAoPinId;
                    if (targetSlot == "alpha")
                        return ui.outputInAlphaPinId;
                    return ui.outputInAlbedoPinId;
                };

                auto targetSlotForOutputPin = [&](int pinId) -> std::string
                {
                    if (pinId == ui.outputInNormalPinId)
                        return "normal";
                    if (pinId == ui.outputInOrmPinId)
                        return "orm";
                    if (pinId == ui.outputInEmissivePinId)
                        return "emissive";
                    if (pinId == ui.outputInRoughnessPinId)
                        return "roughness";
                    if (pinId == ui.outputInMetallicPinId)
                        return "metallic";
                    if (pinId == ui.outputInAoPinId)
                        return "ao";
                    if (pinId == ui.outputInAlphaPinId)
                        return "alpha";
                    return "albedo";
                };

                auto isCoreNodeId = [&](int nodeId) -> bool
                {
                    return nodeId == ui.mappingNodeId || nodeId == ui.texturesNodeId || nodeId == ui.outputNodeId;
                };

                auto drawInputPin = [](int pinId, const char *label)
                {
                    ed::BeginPin(ed::PinId(pinId), ed::PinKind::Input);
                    ImGui::TextUnformatted(label);
                    ed::EndPin();
                };

                bool proceduralGraphChanged = false;
                auto p = gpuMat->params();

                ed::BeginNode(ed::NodeId(ui.mappingNodeId));
                ImGui::TextUnformatted("Mapping");
                ImGui::Separator();

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
                    const VkDescriptorSet descriptorSet = m_assetsPreviewSystem
                                                              ? m_assetsPreviewSystem->getOrRequestTexturePreview(*row.cpuPath, texture)
                                                              : VK_NULL_HANDLE;
                    const bool openTexturePopup = descriptorSet != VK_NULL_HANDLE
                                                      ? ImGui::ImageButton("##thumb", (ImTextureID)(uintptr_t)descriptorSet, ImVec2(44.0f, 44.0f))
                                                      : ImGui::Button("Pick##thumb", ImVec2(44.0f, 44.0f));

                    if (openTexturePopup)
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
                                          glm::value_ptr(dynamicColorNode.params.color),
                                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                    {
                        matEditor.dirty = true;
                        proceduralGraphChanged = true;
                    }

                    if (ImGui::DragFloat("Strength", &dynamicColorNode.params.strength, 0.05f, 0.0f, 64.0f, "%.3f"))
                    {
                        dynamicColorNode.params.strength = std::max(0.0f, dynamicColorNode.params.strength);
                        matEditor.dirty = true;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        proceduralGraphChanged = true;

                    int colorTargetIndex = findTargetSlotIndex(dynamicColorNode.params.targetSlot, colorTargetSlots, 2);
                    drawDeferredEnumSelector("Target", colorTargetDisplay[colorTargetIndex], DeferredEnumPopupKind::ColorTarget, dynamicColorNode.nodeId, 150.0f);

                    int colorBlendIndex = findBlendModeIndex(dynamicColorNode.params.blendMode);
                    drawDeferredEnumSelector("Blend", blendModeLabels[colorBlendIndex], DeferredEnumPopupKind::ColorBlend, dynamicColorNode.nodeId, 150.0f);

                    if (ImGui::Checkbox("Enabled", &dynamicColorNode.params.active))
                    {
                        matEditor.dirty = true;
                        proceduralGraphChanged = true;
                    }

                    ed::BeginPin(ed::PinId(dynamicColorNode.outputPinId), ed::PinKind::Output);
                    ImGui::TextUnformatted("Color");
                    ed::EndPin();
                    ed::EndNode();
                    ImGui::PopID();
                }

                static const char *const noiseTypeLabels[] = {"Value", "Gradient", "FBM", "Voronoi"};

                for (auto &noiseNode : ui.dynamicNoiseNodes)
                {
                    if (noiseNode.pendingPlacement)
                    {
                        ed::SetNodePosition(ed::NodeId(noiseNode.nodeId),
                                            ImVec2(noiseNode.spawnPosition.x, noiseNode.spawnPosition.y));
                        noiseNode.pendingPlacement = false;
                    }

                    ImGui::PushID(noiseNode.nodeId);
                    ed::BeginNode(ed::NodeId(noiseNode.nodeId));
                    ImGui::TextUnformatted("Noise");
                    ImGui::Separator();

                    int typeIdx = static_cast<int>(noiseNode.params.type);
                    drawDeferredEnumSelector("Type", noiseTypeLabels[typeIdx], DeferredEnumPopupKind::NoiseType, noiseNode.nodeId, 120.0f);

                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::DragFloat("Scale", &noiseNode.params.scale, 0.05f, 0.01f, 50.0f, "%.3f"))
                    {
                        noiseNode.params.scale = std::max(0.01f, noiseNode.params.scale);
                        matEditor.dirty = true;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        proceduralGraphChanged = true;

                    if (noiseNode.params.type == engine::CPUMaterial::NoiseNodeParams::Type::FBM)
                    {
                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::SliderInt("Octaves", &noiseNode.params.octaves, 1, 8))
                        {
                            matEditor.dirty = true;
                            proceduralGraphChanged = true;
                        }

                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::DragFloat("Persistence", &noiseNode.params.persistence, 0.01f, 0.05f, 1.0f, "%.3f"))
                        {
                            noiseNode.params.persistence = std::clamp(noiseNode.params.persistence, 0.05f, 1.0f);
                            matEditor.dirty = true;
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            proceduralGraphChanged = true;

                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::DragFloat("Lacunarity", &noiseNode.params.lacunarity, 0.01f, 1.0f, 4.0f, "%.3f"))
                        {
                            noiseNode.params.lacunarity = std::clamp(noiseNode.params.lacunarity, 1.0f, 4.0f);
                            matEditor.dirty = true;
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            proceduralGraphChanged = true;
                    }

                    if (ImGui::Checkbox("World Space", &noiseNode.params.worldSpace))
                    {
                        matEditor.dirty = true;
                        proceduralGraphChanged = true;
                    }

                    int targetIndex = findTargetSlotIndex(noiseNode.params.targetSlot, noiseTargetSlots, 6);
                    drawDeferredEnumSelector("Target", noiseTargetDisplay[targetIndex], DeferredEnumPopupKind::NoiseTarget, noiseNode.nodeId, 150.0f);

                    int noiseBlendIndex = findBlendModeIndex(noiseNode.params.blendMode);
                    drawDeferredEnumSelector("Blend", blendModeLabels[noiseBlendIndex], DeferredEnumPopupKind::NoiseBlend, noiseNode.nodeId, 150.0f);

                    if (isColorTargetSlot(noiseNode.params.targetSlot))
                    {
                        if (ImGui::ColorEdit3("Color A", glm::value_ptr(noiseNode.params.rampColorA)))
                            matEditor.dirty = true;
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            proceduralGraphChanged = true;

                        if (ImGui::ColorEdit3("Color B", glm::value_ptr(noiseNode.params.rampColorB)))
                            matEditor.dirty = true;
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            proceduralGraphChanged = true;
                    }

                    if (ImGui::Checkbox("Enabled##noise", &noiseNode.params.active))
                    {
                        matEditor.dirty = true;
                        proceduralGraphChanged = true;
                    }

                    ed::BeginPin(ed::PinId(noiseNode.outputPinId), ed::PinKind::Output);
                    ImGui::TextUnformatted("Value");
                    ed::EndPin();
                    ed::EndNode();
                    ImGui::PopID();
                }

                ed::BeginNode(ed::NodeId(ui.outputNodeId));
                ImGui::TextUnformatted("Output");
                ImGui::Separator();

                drawInputPin(ui.outputInAlbedoPinId, "Base Color");
                drawInputPin(ui.outputInNormalPinId, "Normal");
                drawInputPin(ui.outputInOrmPinId, "Roughness/AO (ORM)");
                drawInputPin(ui.outputInEmissivePinId, "Emissive");
                drawInputPin(ui.outputInRoughnessPinId, "Roughness");
                drawInputPin(ui.outputInMetallicPinId, "Metallic");
                drawInputPin(ui.outputInAoPinId, "AO");
                drawInputPin(ui.outputInAlphaPinId, "Alpha");
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

                ImGui::TextDisabled("Textures always feed the output. Optional nodes modify linked slots.");
                ed::EndNode();

                auto drawLink = [&](int linkId, int outPinId, int inPinId, ImColor color, float thickness = 2.0f)
                {
                    ed::Link(ed::LinkId(linkId), ed::PinId(outPinId), ed::PinId(inPinId), color, thickness);
                };

                drawLink(ui.linkMappingId, ui.mappingOutVectorPinId, ui.texturesInVectorPinId, ImColor(130, 180, 255));
                for (const auto &row : textureRows)
                    drawLink(row.linkId, row.textureOutputPinId, row.outputInputPinId, ImColor(130, 180, 255));

                for (const auto &dynamicColorNode : ui.dynamicColorNodes)
                {
                    if (!dynamicColorNode.params.active)
                        continue;

                    drawLink(dynamicColorNode.linkId,
                             dynamicColorNode.outputPinId,
                             outputPinForTarget(dynamicColorNode.params.targetSlot),
                             ImColor(255, 190, 110));
                }

                for (const auto &noiseNode : ui.dynamicNoiseNodes)
                {
                    if (!noiseNode.params.active)
                        continue;

                    drawLink(noiseNode.linkId,
                             noiseNode.outputPinId,
                             outputPinForTarget(noiseNode.params.targetSlot),
                             ImColor(255, 160, 60));
                }

                if (ed::BeginCreate())
                {
                    ed::PinId startPinId;
                    ed::PinId endPinId;
                    if (ed::QueryNewLink(&startPinId, &endPinId))
                    {
                        const int startPin = static_cast<int>(startPinId.Get());
                        const int endPin = static_cast<int>(endPinId.Get());
                        const bool touchesCoreLink =
                            (startPin == ui.mappingOutVectorPinId && endPin == ui.texturesInVectorPinId) ||
                            (endPin == ui.mappingOutVectorPinId && startPin == ui.texturesInVectorPinId);

                        bool accepted = false;
                        auto tryAcceptDynamicTargetLink = [&](auto &nodes, auto validTargetPredicate)
                        {
                            for (auto &node : nodes)
                            {
                                const bool fromNode =
                                    (startPin == node.outputPinId) || (endPin == node.outputPinId);
                                if (!fromNode)
                                    continue;

                                const int otherPin = startPin == node.outputPinId ? endPin : startPin;
                                if (!validTargetPredicate(otherPin))
                                    continue;

                                if (ed::AcceptNewItem())
                                {
                                    node.params.targetSlot = targetSlotForOutputPin(otherPin);
                                    node.params.active = true;
                                    matEditor.dirty = true;
                                    proceduralGraphChanged = true;
                                }
                                accepted = true;
                                break;
                            }
                        };

                        if (touchesCoreLink)
                        {
                            ed::RejectNewItem(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), 1.5f);
                            accepted = true;
                        }
                        else
                        {
                            tryAcceptDynamicTargetLink(
                                ui.dynamicNoiseNodes,
                                [&](int pinId)
                                {
                                    return pinId == ui.outputInAlbedoPinId ||
                                           pinId == ui.outputInEmissivePinId ||
                                           pinId == ui.outputInRoughnessPinId ||
                                           pinId == ui.outputInMetallicPinId ||
                                           pinId == ui.outputInAoPinId ||
                                           pinId == ui.outputInAlphaPinId;
                                });

                            if (!accepted)
                            {
                                tryAcceptDynamicTargetLink(
                                    ui.dynamicColorNodes,
                                    [&](int pinId)
                                    {
                                        return pinId == ui.outputInAlbedoPinId || pinId == ui.outputInEmissivePinId;
                                    });
                            }
                        }

                        if (!accepted)
                            ed::RejectNewItem(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), 1.5f);
                    }
                }
                ed::EndCreate();

                if (ed::BeginDelete())
                {
                    ed::LinkId deletedLinkId;
                    while (ed::QueryDeletedLink(&deletedLinkId))
                    {
                        const int deletedLink = static_cast<int>(deletedLinkId.Get());
                        const bool isFixedLink =
                            deletedLink == ui.linkMappingId ||
                            deletedLink == ui.linkAlbedoId ||
                            deletedLink == ui.linkNormalId ||
                            deletedLink == ui.linkOrmId ||
                            deletedLink == ui.linkEmissiveId;
                        if (isFixedLink)
                        {
                            ed::RejectDeletedItem();
                            continue;
                        }

                        bool handled = false;
                        for (auto &dynamicColorNode : ui.dynamicColorNodes)
                        {
                            if (deletedLink != dynamicColorNode.linkId)
                                continue;

                            if (ed::AcceptDeletedItem())
                            {
                                dynamicColorNode.params.active = false;
                                matEditor.dirty = true;
                                proceduralGraphChanged = true;
                            }
                            handled = true;
                            break;
                        }

                        if (handled)
                            continue;

                        for (auto &noiseNode : ui.dynamicNoiseNodes)
                        {
                            if (deletedLink != noiseNode.linkId)
                                continue;

                            if (ed::AcceptDeletedItem())
                            {
                                noiseNode.params.active = false;
                                matEditor.dirty = true;
                                proceduralGraphChanged = true;
                            }
                            handled = true;
                            break;
                        }

                        if (!handled)
                            ed::RejectDeletedItem();
                    }

                    ed::NodeId deletedNodeId;
                    while (ed::QueryDeletedNode(&deletedNodeId))
                    {
                        const int deletedNode = static_cast<int>(deletedNodeId.Get());
                        if (isCoreNodeId(deletedNode))
                        {
                            ed::RejectDeletedItem();
                            continue;
                        }

                        auto removeNode = [&](auto &nodes) -> bool
                        {
                            auto iterator = std::find_if(nodes.begin(), nodes.end(), [&](const auto &node)
                                                         { return node.nodeId == deletedNode; });
                            if (iterator == nodes.end())
                                return false;

                            if (ed::AcceptDeletedItem())
                            {
                                nodes.erase(iterator);
                                matEditor.dirty = true;
                                proceduralGraphChanged = true;
                            }
                            return true;
                        };

                        if (removeNode(ui.dynamicColorNodes))
                            continue;

                        if (removeNode(ui.dynamicNoiseNodes))
                            continue;

                        ed::RejectDeletedItem();
                    }
                }
                ed::EndDelete();

                if (proceduralGraphChanged)
                    compileAndApply();

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
                            newColorNode.params.color = glm::vec3(1.0f, 1.0f, 1.0f);
                            newColorNode.params.strength = 1.0f;
                            newColorNode.params.targetSlot = "albedo";
                            newColorNode.params.blendMode = engine::CPUMaterial::ColorNodeParams::BlendMode::Multiply;
                            newColorNode.params.active = true;
                            const ImVec2 canvasPosition = ed::ScreenToCanvas(ImGui::GetMousePos());
                            newColorNode.spawnPosition = glm::vec2(canvasPosition.x, canvasPosition.y);
                            newColorNode.pendingPlacement = true;
                            ui.dynamicColorNodes.push_back(newColorNode);
                            matEditor.dirty = true;
                            compileAndApply();
                        }
                        if (ImGui::MenuItem("Noise"))
                        {
                            MaterialEditorUIState::DynamicNoiseNode newNoiseNode;
                            newNoiseNode.nodeId = ui.nextNoiseNodeId++;
                            newNoiseNode.outputPinId = ui.nextNoisePinId++;
                            newNoiseNode.linkId = ui.nextNoiseLinkId++;
                            newNoiseNode.params.active = true;
                            newNoiseNode.params.targetSlot = "roughness";
                            newNoiseNode.params.blendMode = engine::CPUMaterial::NoiseNodeParams::BlendMode::Replace;
                            const ImVec2 canvasPosition = ed::ScreenToCanvas(ImGui::GetMousePos());
                            newNoiseNode.spawnPosition = glm::vec2(canvasPosition.x, canvasPosition.y);
                            newNoiseNode.pendingPlacement = true;
                            ui.dynamicNoiseNodes.push_back(newNoiseNode);
                            matEditor.dirty = true;
                            compileAndApply();
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::EndPopup();
                }

                ed::NodeId contextNodeId;
                if (ed::ShowNodeContextMenu(&contextNodeId))
                {
                    const int nodeId = static_cast<int>(contextNodeId.Get());
                    if (!isCoreNodeId(nodeId))
                        ImGui::OpenPopup(("MaterialNodeContext##" + std::to_string(nodeId)).c_str());
                }

                auto drawDeleteNodePopup = [&](auto &nodes)
                {
                    for (auto &node : nodes)
                    {
                        const std::string popupName = "MaterialNodeContext##" + std::to_string(node.nodeId);
                        if (!ImGui::BeginPopup(popupName.c_str()))
                            continue;

                        if (ImGui::MenuItem("Delete"))
                        {
                            node.removeRequested = true;
                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::EndPopup();
                    }
                };

                drawDeleteNodePopup(ui.dynamicColorNodes);
                drawDeleteNodePopup(ui.dynamicNoiseNodes);

                auto findColorNodeById = [&](int nodeId) -> MaterialEditorUIState::DynamicColorNode *
                {
                    auto iterator = std::find_if(ui.dynamicColorNodes.begin(), ui.dynamicColorNodes.end(),
                                                 [&](auto &node) { return node.nodeId == nodeId; });
                    return iterator != ui.dynamicColorNodes.end() ? &(*iterator) : nullptr;
                };

                auto findNoiseNodeById = [&](int nodeId) -> MaterialEditorUIState::DynamicNoiseNode *
                {
                    auto iterator = std::find_if(ui.dynamicNoiseNodes.begin(), ui.dynamicNoiseNodes.end(),
                                                 [&](auto &node) { return node.nodeId == nodeId; });
                    return iterator != ui.dynamicNoiseNodes.end() ? &(*iterator) : nullptr;
                };

                if (!deferredEnumPopupToOpen.empty())
                    ImGui::OpenPopup(deferredEnumPopupToOpen.c_str());

                for (const auto &popup : deferredEnumPopups)
                {
                    if (!ImGui::BeginPopup(popup.popupName.c_str()))
                        continue;

                    switch (popup.kind)
                    {
                    case DeferredEnumPopupKind::ColorTarget:
                    {
                        if (auto *node = findColorNodeById(popup.nodeId))
                        {
                            const int currentIndex = findTargetSlotIndex(node->params.targetSlot, colorTargetSlots, 2);
                            for (int optionIndex = 0; optionIndex < 2; ++optionIndex)
                            {
                                if (!ImGui::Selectable(colorTargetDisplay[optionIndex], currentIndex == optionIndex))
                                    continue;

                                node->params.targetSlot = colorTargetSlots[optionIndex];
                                node->params.active = true;
                                matEditor.dirty = true;
                                compileAndApply();
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        break;
                    }
                    case DeferredEnumPopupKind::ColorBlend:
                    {
                        if (auto *node = findColorNodeById(popup.nodeId))
                        {
                            const int currentIndex = findBlendModeIndex(node->params.blendMode);
                            for (int optionIndex = 0; optionIndex < 3; ++optionIndex)
                            {
                                if (!ImGui::Selectable(blendModeLabels[optionIndex], currentIndex == optionIndex))
                                    continue;

                                node->params.blendMode = colorBlendModeFromIndex(optionIndex);
                                matEditor.dirty = true;
                                compileAndApply();
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        break;
                    }
                    case DeferredEnumPopupKind::NoiseType:
                    {
                        if (auto *node = findNoiseNodeById(popup.nodeId))
                        {
                            const int currentIndex = static_cast<int>(node->params.type);
                            for (int optionIndex = 0; optionIndex < 4; ++optionIndex)
                            {
                                if (!ImGui::Selectable(noiseTypeLabels[optionIndex], currentIndex == optionIndex))
                                    continue;

                                node->params.type = static_cast<engine::CPUMaterial::NoiseNodeParams::Type>(optionIndex);
                                matEditor.dirty = true;
                                compileAndApply();
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        break;
                    }
                    case DeferredEnumPopupKind::NoiseTarget:
                    {
                        if (auto *node = findNoiseNodeById(popup.nodeId))
                        {
                            const int currentIndex = findTargetSlotIndex(node->params.targetSlot, noiseTargetSlots, 6);
                            for (int optionIndex = 0; optionIndex < 6; ++optionIndex)
                            {
                                if (!ImGui::Selectable(noiseTargetDisplay[optionIndex], currentIndex == optionIndex))
                                    continue;

                                node->params.targetSlot = noiseTargetSlots[optionIndex];
                                node->params.active = true;
                                matEditor.dirty = true;
                                compileAndApply();
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        break;
                    }
                    case DeferredEnumPopupKind::NoiseBlend:
                    {
                        if (auto *node = findNoiseNodeById(popup.nodeId))
                        {
                            const int currentIndex = findBlendModeIndex(node->params.blendMode);
                            for (int optionIndex = 0; optionIndex < 3; ++optionIndex)
                            {
                                if (!ImGui::Selectable(blendModeLabels[optionIndex], currentIndex == optionIndex))
                                    continue;

                                node->params.blendMode = noiseBlendModeFromIndex(optionIndex);
                                matEditor.dirty = true;
                                compileAndApply();
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        break;
                    }
                    }

                    ImGui::EndPopup();
                }

                for (size_t nodeIndex = 0; nodeIndex < ui.dynamicColorNodes.size(); ++nodeIndex)
                {
                    if (!ui.dynamicColorNodes[nodeIndex].removeRequested)
                        continue;

                    ui.dynamicColorNodes[nodeIndex].removeRequested = false;
                    ui.dynamicColorNodes[nodeIndex].params.active = false;
                    ui.dynamicColorNodes.erase(ui.dynamicColorNodes.begin() + nodeIndex);
                    matEditor.dirty = true;
                    compileAndApply();
                    break;
                }

                for (size_t nodeIndex = 0; nodeIndex < ui.dynamicNoiseNodes.size(); ++nodeIndex)
                {
                    if (!ui.dynamicNoiseNodes[nodeIndex].removeRequested)
                        continue;

                    ui.dynamicNoiseNodes[nodeIndex].removeRequested = false;
                    ui.dynamicNoiseNodes[nodeIndex].params.active = false;
                    ui.dynamicNoiseNodes.erase(ui.dynamicNoiseNodes.begin() + nodeIndex);
                    matEditor.dirty = true;
                    compileAndApply();
                    break;
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
                        const auto textureAssetPaths = gatherProjectTextureAssets(*m_project, projectRoot);
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
                            const VkDescriptorSet previewDS = m_assetsPreviewSystem
                                                                  ? m_assetsPreviewSystem->getOrRequestTexturePreview(normalizedTexturePath, previewTexture)
                                                                  : VK_NULL_HANDLE;
                            const bool selected = currentTextureReference == textureReferencePath;
                            if (selected)
                                ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 200, 255, 255));

                            const bool pickTexture = previewDS != VK_NULL_HANDLE
                                                         ? ImGui::ImageButton("##pick", (ImTextureID)(uintptr_t)previewDS, ImVec2(44.0f, 44.0f))
                                                         : ImGui::Button("Pick##pick", ImVec2(44.0f, 44.0f));
                            if (pickTexture)
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
        closeCurrentMaterialEditor();
        return;
    }

    if (!matEditor.confirmCloseRequested)
        return;

    const std::string popupId = "Unsaved Changes##Mat_" + matEditor.path.string();
    ImGui::OpenPopup(popupId.c_str());
    matEditor.confirmCloseRequested = false;

    if (ImGui::BeginPopupModal(popupId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("'%s' has unsaved changes.", matEditor.path.filename().string().c_str());
        ImGui::Text("Do you want to save before closing?");
        ImGui::Separator();

        auto project = m_project;
        const std::filesystem::path projectRoot = project ? resolveProjectRootPath(*project) : std::filesystem::path{};
        const std::string normalizedMatPath = resolveMaterialPathAgainstProjectRoot(matEditor.path.string(), projectRoot);

        if (ImGui::Button("Save"))
        {
            auto materialRecordIt = project ? project->cache.materialsByPath.find(normalizedMatPath) : project->cache.materialsByPath.end();
            if (project && materialRecordIt != project->cache.materialsByPath.end())
                saveMaterialToDisk(matEditor.path, materialRecordIt->second.cpuData);
            closeCurrentMaterialEditor();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard"))
        {
            closeCurrentMaterialEditor();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

ELIX_NESTED_NAMESPACE_END
