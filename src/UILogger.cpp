#include "UILogger.hpp"
#include "imgui.h"
#include <VelixFlow/Logger.hpp>
#include <time.h>

void elixUI::UILogger::draw()
{
    // TODO Add clear button
    // if (ImGui::Button("Clear")) {
    //     Logger::instance().clear();
    // }
    // ImGui::SameLine();

    // TODO: Add filtering
    //  ImGui::Checkbox("Info", &show_info);
    //  ImGui::SameLine();
    //  ImGui::Checkbox("Warnings", &show_warnings);
    //  ImGui::SameLine();
    //  ImGui::Checkbox("Errors", &show_errors);
    //  Add copy button
    //  if (ImGui::Button("Copy to Clipboard"))
    //  {
    //      copyLogsToClipboard();
    //  }

    // ImGui::Separator();

    ImGui::BeginChild("LogContent", ImVec2(0, 0), false,
                        ImGuiWindowFlags_HorizontalScrollbar);

    const auto messages = elix::Logger::instance().getMessages();
    for (const auto &msg : messages)
    {
        auto time_t = std::chrono::system_clock::to_time_t(msg.timestamp);
        char time_str[20];
        std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&time_t));

        ImGui::TextDisabled("[%s] ", time_str);
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Text,
                                ImVec4(msg.color.r, msg.color.g, msg.color.b, 1.0f));
        ImGui::TextUnformatted(msg.message.c_str());
        ImGui::PopStyleColor();
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}