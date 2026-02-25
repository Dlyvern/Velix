#include "Editor/AssetsWindow.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cctype>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

namespace
{
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

    std::filesystem::path makeUniqueMaterialPath(const std::filesystem::path &directory, const std::string &baseName)
    {
        return makeUniquePathWithExtension(directory, baseName, ".elixmat");
    }

    std::filesystem::path makeUniqueDuplicatePath(const std::filesystem::path &sourcePath)
    {
        const auto parentPath = sourcePath.parent_path();
        const auto stem = sourcePath.stem().string();
        const auto extension = sourcePath.has_extension() ? sourcePath.extension().string() : "";

        std::filesystem::path candidate = parentPath / (stem + "_Copy" + extension);

        if (!std::filesystem::exists(candidate))
            return candidate;

        uint32_t suffix = 1;

        while (true)
        {
            candidate = parentPath / (stem + "_Copy_" + std::to_string(suffix) + extension);

            if (!std::filesystem::exists(candidate))
                return candidate;

            ++suffix;
        }
    }

    bool writeDefaultMaterialAsset(const std::filesystem::path &materialPath, const std::string &albedoTexturePath = "")
    {
        nlohmann::json json;
        json["name"] = materialPath.stem().string();
        json["texture_path"] = albedoTexturePath;
        json["normal_texture"] = "";
        json["orm_texture"] = "";
        json["emissive_texture"] = "";
        json["color"] = {1.0f, 1.0f, 1.0f, 1.0f};
        json["emissive"] = {0.0f, 0.0f, 0.0f};
        json["metallic"] = 0.0f;
        json["roughness"] = 1.0f;
        json["ao_strength"] = 1.0f;
        json["normal_scale"] = 1.0f;
        json["alpha_cutoff"] = 0.5f;
        json["flags"] = 0u;
        json["uv_scale"] = {1.0f, 1.0f};
        json["uv_offset"] = {0.0f, 0.0f};

        std::ofstream file(materialPath);
        if (!file.is_open())
            return false;

        file << std::setw(4) << json << '\n';
        return file.good();
    }
}

ELIX_NESTED_NAMESPACE_BEGIN(editor)

AssetsWindow::AssetsWindow(EditorResourcesStorage *resourcesStorage, AssetsPreviewSystem &assetsPreviewSystem) : m_resourcesStorage(resourcesStorage),
                                                                                                                 m_assetsPreviewSystem(assetsPreviewSystem)
{
}

void AssetsWindow::setProject(Project *project)
{
    m_currentProject = project;
    m_currentDirectory = m_currentProject->fullPath;
    buildDirectoryTree();
}

void AssetsWindow::draw()
{
    if (!m_currentProject)
        return;

    drawSearchBar();

    ImGui::Separator();

    ImGui::BeginChild("AssetsWindowSplit", ImVec2(0, 0), false);

    ImGui::BeginChild("TreeView", ImVec2(ImGui::GetContentRegionAvail().x * 0.25f, 0), true);
    drawTreeView();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("AssetGrid", ImVec2(0, 0), true);
    drawAssetGrid();

    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (hovered && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        if (ImGui::IsPopupOpen("CreateNewSomething"))
            ImGui::CloseCurrentPopup();
        else
            ImGui::OpenPopup("CreateNewSomething");
    }

    if (ImGui::BeginPopup("CreateNewSomething"))
    {
        if (ImGui::Button("Material"))
        {
            const auto newMaterialPath = makeUniqueMaterialPath(m_currentDirectory, "NewMaterial");

            if (!writeDefaultMaterialAsset(newMaterialPath))
                VX_EDITOR_ERROR_STREAM("Failed to create material asset: " << newMaterialPath << '\n');
            else if (m_onMaterialOpenRequestFunction)
                m_onMaterialOpenRequestFunction(newMaterialPath);

            ImGui::CloseCurrentPopup();
            refreshTree();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();

    ImGui::EndChild();
}

void AssetsWindow::refreshCurrentDirectory()
{
    if (m_currentProject)
    {
        m_currentDirectory = m_currentProject->fullPath;
    }
}

void AssetsWindow::drawSearchBar()
{
    ImGui::Text("Current: ");
    ImGui::SameLine();

    std::filesystem::path currentPath = m_currentDirectory;
    std::filesystem::path projectRoot = m_currentProject->fullPath;

    std::filesystem::path relativePath = std::filesystem::relative(currentPath, projectRoot);

    if (ImGui::SmallButton("Project Root"))
        navigateToDirectory(projectRoot);

    ImGui::SameLine();
    ImGui::Text(" > ");
    ImGui::SameLine();

    std::vector<std::filesystem::path> parts;

    for (const auto &part : relativePath)
        if (!part.empty())
            parts.push_back(part);

    std::filesystem::path accumulatedPath = projectRoot;

    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i > 0)
        {
            ImGui::SameLine();
            ImGui::Text(" > ");
            ImGui::SameLine();
        }

        accumulatedPath /= parts[i];

        if (ImGui::SmallButton(parts[i].string().c_str()))
        {
            navigateToDirectory(accumulatedPath);
        }

        ImGui::SameLine();
    }

    ImGui::NewLine();

    if (ImGui::Button("Up"))
        goUpOneDirectory();

    ImGui::SameLine();

    if (ImGui::Button("Refresh"))
    {
        refreshCurrentDirectory();
        refreshTree();
    }

    ImGui::SameLine();

    float searchWidth = ImGui::GetContentRegionAvail().x - 100.0f;

    ImGui::SetNextItemWidth(searchWidth);

    if (ImGui::InputTextWithHint("##Search", "Search assets...", m_searchBuffer, sizeof(m_searchBuffer)))
        m_searchQuery = m_searchBuffer;

    ImGui::SameLine();

    if (!m_searchQuery.empty())
    {
        if (ImGui::Button("Clear"))
        {
            m_searchQuery.clear();
            memset(m_searchBuffer, 0, sizeof(m_searchBuffer));
        }
    }
}

void AssetsWindow::drawTreeView()
{
    if (!m_treeRoot)
        return;

    ImGui::Text("Project Folders");
    ImGui::Separator();

    std::function<void(TreeNode *)> drawTreeNode = [&](TreeNode *node)
    {
        if (!node)
            return;

        if (m_excludedDirectories.find(node->name) != m_excludedDirectories.end())
            return;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                   ImGuiTreeNodeFlags_SpanFullWidth;

        if (node->children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;

        if (m_selectedTreeNode == node)
            flags |= ImGuiTreeNodeFlags_Selected;

        bool isOpen = ImGui::TreeNodeEx(node->name.c_str(), flags);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            m_selectedTreeNode = node;
            navigateToDirectory(node->path);
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        {
            navigateToDirectory(node->path);
        }

        if (isOpen)
        {
            for (auto &child : node->children)
                drawTreeNode(child.get());

            ImGui::TreePop();
        }

        node->isOpen = isOpen;
    };

    drawTreeNode(m_treeRoot.get());
}

void AssetsWindow::drawAssetGrid()
{
    auto entries = getFilteredEntries();

    if (!m_selectedAssetPath.empty() && !std::filesystem::exists(m_selectedAssetPath))
        m_selectedAssetPath.clear();

    const bool assetGridFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (assetGridFocused && !m_selectedAssetPath.empty() && ImGui::IsKeyPressed(ImGuiKey_F2, false))
        startRenamingAsset(m_selectedAssetPath);
    if (assetGridFocused && !m_selectedAssetPath.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        startDeletingAsset(m_selectedAssetPath);

    if (entries.empty())
    {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           m_searchQuery.empty() ? "No assets in this directory." : "No assets match your search.");
    }
    else
    {
        float windowWidth = ImGui::GetContentRegionAvail().x;
        float itemWidth = 80.0f;
        int columns = std::max(1, (int)(windowWidth / itemWidth));

        ImGui::Columns(columns, "AssetsColumns", false);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        for (const auto &entry : entries)
        {
            const auto assetPath = entry.path();
            std::string id = assetPath.string();
            ImGui::PushID(id.c_str());

            ImGui::BeginGroup();

            VkDescriptorSet icon = m_resourcesStorage->getTextureDescriptorSet("./resources/textures/file.png");
            std::string filename = assetPath.filename().string();
            std::string extension = assetPath.extension().string();
            std::string extensionLower = extension;
            std::transform(extensionLower.begin(), extensionLower.end(), extensionLower.begin(), [](unsigned char character)
                           { return static_cast<char>(std::tolower(character)); });

            if (entry.is_directory())
            {
                icon = m_resourcesStorage->getTextureDescriptorSet("./resources/textures/folder.png");
            }
            else
            {
                if (extensionLower == ".elixmat")
                {
                    icon = m_assetsPreviewSystem.getOrRequestMaterialPreview(assetPath.string());
                }
                else if (m_velixExtensions.find(extensionLower) != m_velixExtensions.end())
                {
                    icon = m_resourcesStorage->getTextureDescriptorSet("./resources/textures/VelixV.png");
                }
                else if (m_textureExtensions.find(extensionLower) != m_textureExtensions.end())
                {
                    icon = m_assetsPreviewSystem.getOrRequestTexturePreview(assetPath.string());
                }
                else if (m_modelExtensions.find(extensionLower) != m_modelExtensions.end())
                {
                    icon = m_assetsPreviewSystem.getOrRequestModelPreview(assetPath.string());
                }
            }

            const bool isSelected = !m_selectedAssetPath.empty() && m_selectedAssetPath == assetPath;
            if (isSelected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.35f, 0.55f, 0.45f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.42f, 0.68f, 0.55f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.19f, 0.30f, 0.48f, 0.65f));
            }

            ImGui::ImageButton(id.c_str(), icon, ImVec2(50, 50));

            if (isSelected)
                ImGui::PopStyleColor(3);

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (entry.is_directory())
                    navigateToDirectory(assetPath);
                else
                {
                    if (extensionLower == ".elixmat" && m_onMaterialOpenRequestFunction)
                        m_onMaterialOpenRequestFunction(assetPath);
                    else
                    {
                        const bool isTextEditable =
                            m_shaderExtensions.find(extensionLower) != m_shaderExtensions.end() ||
                            m_cppExtensions.find(extensionLower) != m_cppExtensions.end() ||
                            m_headerExtensions.find(extensionLower) != m_headerExtensions.end() ||
                            m_configExtensions.find(extensionLower) != m_configExtensions.end() ||
                            m_sceneExtensions.find(extensionLower) != m_sceneExtensions.end() ||
                            m_velixExtensions.find(extensionLower) != m_velixExtensions.end();

                        if (isTextEditable && m_onTextAssetOpenRequestFunction)
                            m_onTextAssetOpenRequestFunction(assetPath);
                    }
                }
            }

            ImGui::TextWrapped("%s", filename.c_str());

            ImGui::EndGroup();

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                m_selectedAssetPath = assetPath;

            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                m_selectedAssetPath = assetPath;
                m_contextAssetPath = assetPath;
                ImGui::OpenPopup("AssetItemContextMenu");
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();

                if (entry.is_directory())
                {
                    ImGui::Text("Folder: %s", filename.c_str());
                    ImGui::Text("Path: %s", assetPath.parent_path().string().c_str());
                }
                else
                {
                    ImGui::Text("File: %s", filename.c_str());
                    ImGui::Text("Size: %s", formatFileSize(entry.file_size()).c_str());
                    ImGui::Text("Type: %s", extension.c_str());
                }

                ImGui::EndTooltip();
            }

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                const std::string filePath = assetPath.string();
                ImGui::SetDragDropPayload("ASSET_PATH", filePath.c_str(), filePath.size() + 1);
                ImGui::Text("Dragging: %s", filename.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::PopID();
            ImGui::NextColumn();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::Columns(1);
    }

    if (ImGui::BeginPopup("AssetItemContextMenu"))
    {
        const bool assetExists = !m_contextAssetPath.empty() && std::filesystem::exists(m_contextAssetPath);

        if (!assetExists)
        {
            ImGui::TextDisabled("Asset no longer exists.");
        }
        else
        {
            const bool isDirectory = std::filesystem::is_directory(m_contextAssetPath);
            std::string extension = m_contextAssetPath.extension().string();
            std::string extensionLower = extension;
            std::transform(extensionLower.begin(), extensionLower.end(), extensionLower.begin(), [](unsigned char character)
                           { return static_cast<char>(std::tolower(character)); });

            if (isDirectory)
            {
                if (ImGui::MenuItem("Open"))
                    navigateToDirectory(m_contextAssetPath);
            }
            else
            {
                if (extensionLower == ".elixmat" && ImGui::MenuItem("Open Material"))
                {
                    if (m_onMaterialOpenRequestFunction)
                        m_onMaterialOpenRequestFunction(m_contextAssetPath);
                }

                const bool isTextEditable =
                    m_shaderExtensions.find(extensionLower) != m_shaderExtensions.end() ||
                    m_cppExtensions.find(extensionLower) != m_cppExtensions.end() ||
                    m_headerExtensions.find(extensionLower) != m_headerExtensions.end() ||
                    m_configExtensions.find(extensionLower) != m_configExtensions.end() ||
                    m_sceneExtensions.find(extensionLower) != m_sceneExtensions.end() ||
                    m_velixExtensions.find(extensionLower) != m_velixExtensions.end();

                if (isTextEditable && m_onTextAssetOpenRequestFunction && ImGui::MenuItem("Open in Editor"))
                    m_onTextAssetOpenRequestFunction(m_contextAssetPath);

                if (m_textureExtensions.find(extensionLower) != m_textureExtensions.end())
                {
                    if (ImGui::MenuItem("Create Material From Texture"))
                        createMaterialFromTexture(m_contextAssetPath);
                }
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Rename", "F2"))
                startRenamingAsset(m_contextAssetPath);

            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !isDirectory))
                duplicateAsset(m_contextAssetPath);

            if (ImGui::MenuItem("Copy Path"))
            {
                const std::string pathString = m_contextAssetPath.string();
                ImGui::SetClipboardText(pathString.c_str());
            }

            if (ImGui::MenuItem("Delete", "Del"))
                startDeletingAsset(m_contextAssetPath);
        }

        ImGui::EndPopup();
    }

    if (m_openRenamePopupRequested)
    {
        ImGui::OpenPopup("Rename Asset");
        m_openRenamePopupRequested = false;
    }

    if (ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Rename: %s", m_renameAssetPath.filename().string().c_str());
        ImGui::Separator();

        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText("##RenameAssetInput", m_renameBuffer, sizeof(m_renameBuffer));

        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere(-1);

        if (ImGui::Button("Rename"))
        {
            if (renameAsset(m_renameAssetPath, m_renameBuffer))
            {
                ImGui::CloseCurrentPopup();
                refreshTree();
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    if (m_openDeletePopupRequested)
    {
        ImGui::OpenPopup("Delete Asset");
        m_openDeletePopupRequested = false;
    }

    if (ImGui::BeginPopupModal("Delete Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("Delete '%s' ?", m_deleteAssetPath.filename().string().c_str());
        if (std::filesystem::is_directory(m_deleteAssetPath))
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "This will delete the entire directory recursively.");

        ImGui::Separator();

        if (ImGui::Button("Delete"))
        {
            if (deleteAsset(m_deleteAssetPath))
            {
                ImGui::CloseCurrentPopup();
                refreshTree();
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

void AssetsWindow::startRenamingAsset(const std::filesystem::path &path)
{
    if (path.empty() || !std::filesystem::exists(path))
        return;

    m_renameAssetPath = path;
    std::memset(m_renameBuffer, 0, sizeof(m_renameBuffer));

    const std::string fileName = path.filename().string();
    std::strncpy(m_renameBuffer, fileName.c_str(), sizeof(m_renameBuffer) - 1);

    m_openRenamePopupRequested = true;
}

void AssetsWindow::startDeletingAsset(const std::filesystem::path &path)
{
    if (path.empty() || !std::filesystem::exists(path))
        return;

    m_deleteAssetPath = path;
    m_openDeletePopupRequested = true;
}

bool AssetsWindow::renameAsset(const std::filesystem::path &path, const std::string &newName)
{
    if (path.empty() || !std::filesystem::exists(path))
        return false;

    auto trimmedName = newName;
    trimmedName.erase(trimmedName.begin(), std::find_if(trimmedName.begin(), trimmedName.end(), [](unsigned char ch)
                                                        { return !std::isspace(ch); }));
    trimmedName.erase(std::find_if(trimmedName.rbegin(), trimmedName.rend(), [](unsigned char ch)
                                   { return !std::isspace(ch); })
                          .base(),
                      trimmedName.end());

    if (trimmedName.empty())
        return false;

    if (trimmedName.find('/') != std::string::npos || trimmedName.find('\\') != std::string::npos)
        return false;

    const std::filesystem::path newPath = path.parent_path() / trimmedName;

    if (newPath == path)
        return true;

    if (std::filesystem::exists(newPath))
        return false;

    try
    {
        std::filesystem::rename(path, newPath);
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        VX_EDITOR_ERROR_STREAM("Failed to rename asset: " << e.what() << '\n');
        return false;
    }

    if (m_selectedAssetPath == path)
        m_selectedAssetPath = newPath;

    if (m_contextAssetPath == path)
        m_contextAssetPath = newPath;

    const std::string oldCurrent = path.lexically_normal().string();
    const std::string activeCurrent = m_currentDirectory.lexically_normal().string();

    if (m_currentDirectory == path)
        m_currentDirectory = newPath;
    else if (activeCurrent.rfind(oldCurrent + "/", 0) == 0)
    {
        const std::string suffix = activeCurrent.substr(oldCurrent.size() + 1);
        m_currentDirectory = newPath / std::filesystem::path(suffix);
    }

    return true;
}

bool AssetsWindow::deleteAsset(const std::filesystem::path &path)
{
    if (path.empty() || !std::filesystem::exists(path))
        return false;

    auto pathContains = [](const std::filesystem::path &parent, const std::filesystem::path &candidate) -> bool
    {
        const std::string parentPath = parent.lexically_normal().string();
        const std::string candidatePath = candidate.lexically_normal().string();

        return candidatePath == parentPath || candidatePath.rfind(parentPath + "/", 0) == 0;
    };

    try
    {
        if (std::filesystem::is_directory(path))
            std::filesystem::remove_all(path);
        else
            std::filesystem::remove(path);
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        VX_EDITOR_ERROR_STREAM("Failed to delete asset: " << e.what() << '\n');
        return false;
    }

    if (!m_selectedAssetPath.empty() && pathContains(path, m_selectedAssetPath))
        m_selectedAssetPath.clear();

    if (!m_contextAssetPath.empty() && pathContains(path, m_contextAssetPath))
        m_contextAssetPath.clear();

    if (!m_currentDirectory.empty() && pathContains(path, m_currentDirectory))
        m_currentDirectory = m_currentProject ? std::filesystem::path(m_currentProject->fullPath) : std::filesystem::current_path();

    return true;
}

bool AssetsWindow::duplicateAsset(const std::filesystem::path &path)
{
    if (path.empty() || !std::filesystem::exists(path) || std::filesystem::is_directory(path))
        return false;

    const auto duplicatePath = makeUniqueDuplicatePath(path);

    try
    {
        std::filesystem::copy_file(path, duplicatePath, std::filesystem::copy_options::none);
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        VX_EDITOR_ERROR_STREAM("Failed to duplicate asset: " << e.what() << '\n');
        return false;
    }

    m_selectedAssetPath = duplicatePath;
    refreshTree();
    return true;
}

bool AssetsWindow::createMaterialFromTexture(const std::filesystem::path &texturePath)
{
    if (texturePath.empty() || !std::filesystem::exists(texturePath) || std::filesystem::is_directory(texturePath))
        return false;

    const auto materialPath = makeUniqueMaterialPath(texturePath.parent_path(), texturePath.stem().string() + "_Material");

    if (!writeDefaultMaterialAsset(materialPath, texturePath.string()))
        return false;

    m_selectedAssetPath = materialPath;
    refreshTree();

    if (m_onMaterialOpenRequestFunction)
        m_onMaterialOpenRequestFunction(materialPath);

    return true;
}

std::vector<std::filesystem::directory_entry> AssetsWindow::getFilteredEntries()
{
    std::vector<std::filesystem::directory_entry> entries;

    try
    {
        for (const auto &entry : std::filesystem::directory_iterator(m_currentDirectory))
        {
            // Skip hidden files/directories
            std::string filename = entry.path().filename().string();

            if (!filename.empty() && filename[0] == '.')
                continue;

            // Skip excluded directories
            if (entry.is_directory() &&
                m_excludedDirectories.find(filename) != m_excludedDirectories.end())
                continue;

            // Apply search filter if query exists
            if (!m_searchQuery.empty() && !matchesSearch(filename))
                continue;

            entries.push_back(entry);
        }

        std::sort(entries.begin(), entries.end(),
                  [](const std::filesystem::directory_entry &a, const std::filesystem::directory_entry &b)
                  {
                      if (a.is_directory() != b.is_directory())
                          return a.is_directory(); // Directories first
                      return a.path().filename() < b.path().filename();
                  });
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        VX_EDITOR_ERROR_STREAM("Error reading directory: " << e.what() << std::endl);
    }

    return entries;
}

bool AssetsWindow::matchesSearch(const std::string &filename) const
{
    if (m_searchQuery.empty())
        return true;

    std::string lowerFilename = filename;
    std::string lowerQuery = m_searchQuery;

    std::transform(lowerFilename.begin(), lowerFilename.end(),
                   lowerFilename.begin(), ::tolower);
    std::transform(lowerQuery.begin(), lowerQuery.end(),
                   lowerQuery.begin(), ::tolower);

    return lowerFilename.find(lowerQuery) != std::string::npos;
}

void AssetsWindow::buildDirectoryTree()
{
    if (!m_currentProject)
        return;

    m_treeRoot = std::make_shared<TreeNode>(
        m_currentProject->name,
        m_currentProject->fullPath);

    buildTreeNode(m_treeRoot.get(), m_currentProject->fullPath);
    syncTreeWithCurrentDirectory();
}

void AssetsWindow::buildTreeNode(TreeNode *node, const std::filesystem::path &path)
{
    try
    {
        for (const auto &entry : std::filesystem::directory_iterator(path))
        {
            if (!entry.is_directory())
                continue;

            std::string dirName = entry.path().filename().string();

            // Skip excluded directories
            if (m_excludedDirectories.find(dirName) != m_excludedDirectories.end())
                continue;

            // Skip hidden directories
            if (!dirName.empty() && dirName[0] == '.')
                continue;

            auto child = std::make_shared<TreeNode>(dirName, entry.path());
            child->parent = node;
            node->children.push_back(child);

            // Recursively build tree (optional - can be done on demand)
            // buildTreeNode(child.get(), entry.path());
        }

        // Sort children alphabetically
        std::sort(node->children.begin(), node->children.end(),
                  [](const std::shared_ptr<TreeNode> &a,
                     const std::shared_ptr<TreeNode> &b)
                  {
                      return a->name < b->name;
                  });
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        VX_EDITOR_ERROR_STREAM("Error building tree node: " << e.what() << std::endl);
    }
}

void AssetsWindow::syncTreeWithCurrentDirectory()
{
    if (!m_treeRoot || !m_currentProject)
        return;

    std::function<TreeNode *(TreeNode *, const std::filesystem::path &)> findNode =
        [&](TreeNode *node, const std::filesystem::path &targetPath) -> TreeNode *
    {
        if (node->path == targetPath)
            return node;

        for (auto &child : node->children)
        {
            if (targetPath.string().find(child->path.string()) == 0)
            {
                TreeNode *found = findNode(child.get(), targetPath);
                if (found)
                    return found;
            }
        }
        return nullptr;
    };

    m_selectedTreeNode = findNode(m_treeRoot.get(), m_currentDirectory);
}

void AssetsWindow::navigateToDirectory(const std::filesystem::path &path)
{
    // if (m_currentDirectory.has_parent_path() &&
    //     m_currentDirectory != m_currentProject->fullPath)
    // {
    m_currentDirectory = std::filesystem::absolute(path);
    syncTreeWithCurrentDirectory();
    // }
}

void AssetsWindow::goUpOneDirectory()
{
    if (m_currentDirectory.has_parent_path() &&
        m_currentDirectory != m_currentProject->fullPath)
    {
        navigateToDirectory(m_currentDirectory.parent_path());
    }
}

void AssetsWindow::refreshTree()
{
    if (m_currentProject)
    {
        buildDirectoryTree();
    }
}

std::string AssetsWindow::formatFileSize(uintmax_t size) const
{
    const char *suffixes[] = {"B", "KB", "MB", "GB", "TB"};
    int suffixIndex = 0;
    double sizeDbl = static_cast<double>(size);

    while (sizeDbl >= 1024.0 && suffixIndex < 4)
    {
        sizeDbl /= 1024.0;
        suffixIndex++;
    }

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.2f %s", sizeDbl, suffixes[suffixIndex]);
    return std::string(buffer);
}

ELIX_NESTED_NAMESPACE_END
