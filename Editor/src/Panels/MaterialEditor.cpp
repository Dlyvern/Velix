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
        normalizedPath = resolveMaterialPathAgainstProjectRoot(path.string(), std::filesystem::path(m_project->fullPath));

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
        normalizedPath = resolveMaterialPathAgainstProjectRoot(path.string(), std::filesystem::path(m_project->fullPath));
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

    const std::filesystem::path projectRoot = std::filesystem::path(m_project->fullPath);

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
                        notify(NotificationType::Success, "Material saved");
                        VX_EDITOR_INFO_STREAM("Material saved: " << normalizedMatPath);
                    }
                    else
                    {
                        notify(NotificationType::Error, "Failed to save material");
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
                        auto refreshedIt = m_project->cache.materialsByPath.find(normalizedMatPath);
                        if (refreshedIt != m_project->cache.materialsByPath.end())
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

                            const std::string functions = ui.customFunctionsEditor.GetText();
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
                                for (unsigned char c : hashInput)
                                {
                                    hash ^= c;
                                    hash *= 1099511628211ULL;
                                }
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
                                    notify(NotificationType::Success, "Custom shader compiled and applied");
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
                                return;

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
        const std::filesystem::path projectRoot = project ? std::filesystem::path(project->fullPath) : std::filesystem::path{};
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
