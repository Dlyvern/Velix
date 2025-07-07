#include "UITerminal.hpp"
#include "imgui.h"
#include <vector>
#include <memory>
#include <array>

void elixUI::UITerminal::draw()
{
    static char commandBuffer[256] = "";
    static std::vector<std::string> history;

    ImGui::BeginChild("TerminalOutput", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));

    for (const auto &line : history)
        ImGui::TextUnformatted(line.c_str());

    ImGui::EndChild();

    ImGui::PushItemWidth(-1);

    if (ImGui::InputText("##Command", commandBuffer, IM_ARRAYSIZE(commandBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        history.push_back("> " + std::string(commandBuffer));

        constexpr int kBufferSize = 128;
        std::array<char, kBufferSize> buffer{};
        std::string result;

#ifdef _WIN32
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
#else
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(commandBuffer, "r"), pclose);
#endif

        if (pipe)
        {
            while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
            {
                result += buffer.data();
            }

            if (!result.empty())
            {
                history.push_back(result);
            }

            commandBuffer[0] = '\0';
            ImGui::SetKeyboardFocusHere(-1);
        }
    }

    ImGui::PopItemWidth();
}