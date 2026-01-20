#include "Editor/AssetsWindow.hpp"
#include <imgui.h>
#include <imgui_internal.h>

#include <vector>
#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

AssetsWindow::AssetsWindow(EditorResourcesStorage *resourcesStorage) : m_resourcesStorage(resourcesStorage)
{
}

void AssetsWindow::setProject(engine::Project *project)
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

    if (entries.empty())
    {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           m_searchQuery.empty() ? "No assets in this directory." : "No assets match your search.");
        return;
    }

    float windowWidth = ImGui::GetContentRegionAvail().x;
    float itemWidth = 80.0f;
    int columns = std::max(1, (int)(windowWidth / itemWidth));

    ImGui::Columns(columns, "AssetsColumns", false);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

    for (const auto &entry : entries)
    {
        std::string id = entry.path().string();
        ImGui::PushID(id.c_str());

        ImGui::BeginGroup();

        VkDescriptorSet icon = m_resourcesStorage->getTextureDescriptorSet("./resources/textures/file.png");
        std::string filename = entry.path().filename().string();
        std::string extension = entry.path().extension().string();

        if (entry.is_directory())
        {
            icon = m_resourcesStorage->getTextureDescriptorSet("./resources/textures/folder.png");
        }
        else
        {
            // Choose icon based on file extension
            // if (m_cppExtensions.find(extension) != m_cppExtensions.end())
            //     icon = m_cppIcon;
            // else if (m_headerExtensions.find(extension) != m_headerExtensions.end())
            //     icon = m_headerIcon;
            // else if (m_imageExtensions.find(extension) != m_imageExtensions.end())
            //     icon = m_imageIcon;
            // else if (m_modelExtensions.find(extension) != m_modelExtensions.end())
            //     icon = m_modelIcon;
            // else if (m_sceneExtensions.find(extension) != m_sceneExtensions.end())
            //     icon = m_sceneIcon;
            // else if (m_shaderExtensions.find(extension) != m_shaderExtensions.end())
            //     icon = m_shaderIcon;
            // else if (m_configExtensions.find(extension) != m_configExtensions.end())
            //     icon = m_configIcon;
        }

        ImGui::ImageButton(id.c_str(), icon, ImVec2(50, 50));

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (entry.is_directory())
                navigateToDirectory(entry.path());
            // else
            // {
            // Handle file click (open in editor, etc.)
            // You can implement this based on your needs
            // }
        }

        ImGui::TextWrapped("%s", filename.c_str());

        ImGui::EndGroup();

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();

            if (entry.is_directory())
            {
                ImGui::Text("Folder: %s", filename.c_str());
                ImGui::Text("Path: %s", entry.path().parent_path().string().c_str());
            }
            else
            {
                ImGui::Text("File: %s", filename.c_str());
                ImGui::Text("Size: %s", formatFileSize(entry.file_size()).c_str());
                ImGui::Text("Type: %s", extension.c_str());
                ImGui::Text("Modified: %s",
                            std::to_string(std::filesystem::last_write_time(entry.path()).time_since_epoch().count()).c_str());
            }

            ImGui::EndTooltip();
        }

        //! DOES NOT WORK Drag and drop source
        // if (ImGui::BeginDragDropSource())
        // {
        //     std::string filePath = entry.path().string();
        //     ImGui::SetDragDropPayload("ASSET_PATH", filePath.data(),
        //                               filePath.size() + 1);
        //     ImGui::Text("Dragging: %s", filename.c_str());
        //     ImGui::EndDragDropSource();
        // }

        ImGui::PopID();
        ImGui::NextColumn();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::Columns(1);
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
        std::cerr << "Error reading directory: " << e.what() << std::endl;
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
        std::cerr << "Error building tree node: " << e.what() << std::endl;
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