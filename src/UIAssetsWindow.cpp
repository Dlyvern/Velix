#include "UIAssetsWindow.hpp"
#include "ProjectManager.hpp"
#include "imgui.h"
#include <filesystem>
#include <VelixFlow/Filesystem.hpp>

void elixUI::UIAssetsWindow::draw()
{
     static auto folderTexture = ProjectManager::instance().getAssetsCache()->getAsset<elix::AssetTexture>(elix::filesystem::getExecutablePath().string() + "/resources/textures/folder.png");
    static auto fileTexture = ProjectManager::instance().getAssetsCache()->getAsset<elix::AssetTexture>(elix::filesystem::getExecutablePath().string() + "/resources/textures/file.png");

    if (folderTexture && !folderTexture->getTexture()->isBaked())
    {
        folderTexture->getTexture()->create();
        folderTexture->getTexture()->addDefaultParameters();
        folderTexture->getTexture()->bake();
    }
    if (fileTexture && !fileTexture->getTexture()->isBaked())
    {
        fileTexture->getTexture()->create();
        fileTexture->getTexture()->addDefaultParameters();
        fileTexture->getTexture()->bake();
    }

    ImTextureID folderIcon = folderTexture ? static_cast<ImTextureID>(static_cast<intptr_t>(folderTexture->getTexture()->getId())) : 0;
    ImTextureID fileIcon = fileTexture ? static_cast<ImTextureID>(static_cast<intptr_t>(fileTexture->getTexture()->getId())) : 0;

    const float iconSize = 64.0f;
    const float padding = 16.0f;

    // TODO CHANGE IT LATER
    if (m_assetsPath.empty())
        m_assetsPath = ProjectManager::instance().getCurrentProject()->fullPath;

    if (m_assetsPath.has_parent_path() && m_assetsPath != ProjectManager::instance().getCurrentProject()->fullPath)
        if (ImGui::Button(".."))
            m_assetsPath = m_assetsPath.parent_path();

    float cellSize = iconSize + padding;
    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = std::max(1, (int)(panelWidth / cellSize));

    int itemIndex = 0;
    ImGui::Columns(columnCount, nullptr, false);

    const std::unordered_set<std::string> restrictedExtensions{
       ".elixirproject"
    };

    for (const auto& entry : std::filesystem::directory_iterator(m_assetsPath))
    {
        const auto& path = entry.path();

        if (!is_directory(path))
        {
            auto ext = path.extension().string();

            if (restrictedExtensions.contains(ext))
                continue;
        }

        std::string name = path.filename().string();

        ImGui::PushID(name.c_str());

        ImTextureID icon = entry.is_directory() ? folderIcon : fileIcon;

        ImGui::ImageButton("", icon,
                           ImVec2(iconSize, iconSize),
                           ImVec2(0, 0), ImVec2(1, 1),
                           ImVec4(0, 0, 0, 0),
                           ImVec4(1, 1, 1, 1));


        if (ImGui::BeginPopupContextItem("AssetContextMenu"))
        {
            if (ImGui::MenuItem("Open in Editor"))
            {
                std::string fullPath = path.string();

                if (fullPath.ends_with(".cpp") || fullPath.ends_with(".hpp"))
                    m_fileEditorPath = fullPath;
            }

            if (ImGui::MenuItem("Show in File Manager"))
                elix::filesystem::openInFileManager(path);

            ImGui::EndPopup();
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            m_draggingInfo.name = path.string();

            ImGui::SetDragDropPayload("ASSET_PATH", &m_draggingInfo, sizeof(m_draggingInfo));
            ImGui::Text("Dragging: %s", name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
        {
            if (entry.is_directory())
            {
            }
            else
            {
            }
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        {
            if (entry.is_directory())
            {
                m_assetsPath = path;
            }
        }

        ImGui::TextWrapped("%s", name.c_str());

        ImGui::NextColumn();
        ImGui::PopID();
        itemIndex++;
    }

    ImGui::Columns(1);
}