#include "Editor/Panels/LightmapBakingPanel.hpp"

#include <imgui.h>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

void LightmapBakingPanel::draw(bool *open)
{
    if (!open || !*open)
        return;

    ImGui::SetNextWindowSize(ImVec2(340, 280), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(100, 120), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Lightmap Baking", open))
    {
        ImGui::End();
        return;
    }


    ImGui::SeparatorText("Settings");

    const char *resItems[] = {"256", "512", "1024", "2048", "4096"};
    const int   resValues[] = {256, 512, 1024, 2048, 4096};
    int currentResIdx = 2;
    for (int i = 0; i < 5; ++i)
        if (resValues[i] == m_resolution)
        { currentResIdx = i; break; }

    if (ImGui::BeginCombo("Resolution", resItems[currentResIdx]))
    {
        for (int i = 0; i < 5; ++i)
        {
            const bool selected = (resValues[i] == m_resolution);
            if (ImGui::Selectable(resItems[i], selected))
                m_resolution = resValues[i];
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Checkbox("Dilation", &m_dilate);
    if (m_dilate)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::DragInt("Radius", &m_dilateRadius, 1, 1, 16);
    }


    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const bool baking = m_progress && !m_progress->done.load();
    if (baking)
        ImGui::BeginDisabled();

    if (ImGui::Button("Bake Lighting", ImVec2(-1.0f, 0.0f)))
    {
        if (m_onBakeRequested)
        {
            engine::LightmapBakeSettings settings;
            settings.resolution   = static_cast<uint32_t>(m_resolution);
            settings.dilate       = m_dilate;
            settings.dilateRadius = static_cast<uint32_t>(m_dilateRadius);
            m_onBakeRequested(settings);
        }
    }

    if (baking)
        ImGui::EndDisabled();


    if (m_progress)
    {
        ImGui::Spacing();
        ImGui::SeparatorText("Progress");

        const uint32_t total     = m_progress->totalEntities.load();
        const uint32_t completed = m_progress->completedEntities.load();
        const bool     done      = m_progress->done.load();

        if (total > 0)
        {
            const float fraction = static_cast<float>(completed) / static_cast<float>(total);
            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%u / %u", completed, total);
            ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
        }
        else
        {
            ImGui::ProgressBar(0.0f, ImVec2(-1.0f, 0.0f), "");
        }

        if (done)
        {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Bake complete!");
        }
        else if (baking)
        {
            std::string entityName;
            {
                std::lock_guard<std::mutex> lock(m_progress->statusMutex);
                entityName = m_progress->currentEntityName;
            }
            if (!entityName.empty())
                ImGui::Text("Baking: %s", entityName.c_str());
            else
                ImGui::Text("Preparing...");
        }
    }

    ImGui::End();
}

ELIX_NESTED_NAMESPACE_END
