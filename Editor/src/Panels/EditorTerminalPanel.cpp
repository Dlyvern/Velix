#include "Editor/Editor.hpp"
#include "Editor/FileHelper.hpp"
#include "Core/Logger.hpp"
#include "Engine/Shaders/ShaderCompiler.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

ELIX_NESTED_NAMESPACE_BEGIN(editor)
void Editor::drawTerminal()
{
    if (!m_showTerminal)
        return;

    ImGui::Begin("Terminal with logs", &m_showTerminal);

    auto *logger = core::Logger::getDefaultLogger();
    if (!logger)
    {
        ImGui::TextDisabled("Logger is not initialized");
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear Logs"))
    {
        logger->clearHistory();
        m_terminalLastLogCount = 0;
        m_forceTerminalScrollToBottom = true;
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("Auto-scroll", &m_terminalAutoScroll) && m_terminalAutoScroll)
        m_forceTerminalScrollToBottom = true;

    ImGui::SameLine();
    ImGui::Checkbox("Clear input on run", &m_terminalClearInputOnSubmit);

    auto drawLayerToggle = [this](const char *label, int bit)
    {
        bool enabled = (m_terminalSelectedLayerMask & (1 << bit)) != 0;
        if (ImGui::Checkbox(label, &enabled))
        {
            if (enabled)
                m_terminalSelectedLayerMask |= (1 << bit);
            else
                m_terminalSelectedLayerMask &= ~(1 << bit);
        }
    };

    drawLayerToggle("Core", 0);
    ImGui::SameLine();
    drawLayerToggle("Engine", 1);
    ImGui::SameLine();
    drawLayerToggle("Editor", 2);
    ImGui::SameLine();
    drawLayerToggle("Developer", 3);
    ImGui::SameLine();
    drawLayerToggle("User", 4);

    const float commandLineHeight = ImGui::GetFrameHeightWithSpacing() * 2.2f;
    if (ImGui::BeginChild("TerminalLogRegion", ImVec2(0, -commandLineHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
    {
        const auto logs = logger->getHistorySnapshot();
        const size_t currentLogCount = logs.size();
        const bool hasNewLogs = currentLogCount > m_terminalLastLogCount;
        const bool historyShrank = currentLogCount < m_terminalLastLogCount;

        const float scrollYBeforeRender = ImGui::GetScrollY();
        const float scrollMaxBeforeRender = ImGui::GetScrollMaxY();
        const bool wasAtBottom = (scrollMaxBeforeRender <= 0.0f) || (scrollYBeforeRender >= scrollMaxBeforeRender - 1.0f);

        for (const auto &logMessage : logs)
        {
            int layerBit = 3;
            switch (logMessage.layer)
            {
            case core::Logger::LogLayer::Core:
                layerBit = 0;
                break;
            case core::Logger::LogLayer::Engine:
                layerBit = 1;
                break;
            case core::Logger::LogLayer::Editor:
                layerBit = 2;
                break;
            case core::Logger::LogLayer::Developer:
                layerBit = 3;
                break;
            case core::Logger::LogLayer::User:
                layerBit = 4;
                break;
            }

            if ((m_terminalSelectedLayerMask & (1 << layerBit)) == 0)
                continue;

            ImVec4 color = ImVec4(0.85f, 0.88f, 0.93f, 1.0f);
            switch (logMessage.level)
            {
            case core::Logger::LogLevel::DEBUG:
                color = ImVec4(0.45f, 0.70f, 1.0f, 1.0f);
                break;
            case core::Logger::LogLevel::INFO:
                color = ImVec4(0.74f, 0.90f, 0.78f, 1.0f);
                break;
            case core::Logger::LogLevel::WARNING:
                color = ImVec4(1.0f, 0.86f, 0.52f, 1.0f);
                break;
            case core::Logger::LogLevel::LOG_LEVEL_ERROR:
                color = ImVec4(1.0f, 0.46f, 0.46f, 1.0f);
                break;
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(logMessage.formattedMessage.c_str());
            ImGui::PopStyleColor();
        }

        const bool shouldAutoScroll = m_terminalAutoScroll &&
                                      (m_forceTerminalScrollToBottom || historyShrank || (hasNewLogs && wasAtBottom));

        if (shouldAutoScroll)
            ImGui::SetScrollHereY(1.0f);

        m_terminalLastLogCount = currentLogCount;
        m_forceTerminalScrollToBottom = false;
    }
    ImGui::EndChild();

    ImGui::PushItemWidth(-70.0f);
    const bool submitWithEnter = ImGui::InputText("##TerminalCommand", m_terminalCommandBuffer, sizeof(m_terminalCommandBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();

    ImGui::SameLine();
    const bool submitWithButton = ImGui::Button("Run");

    if (submitWithEnter || submitWithButton)
    {
        std::string command = m_terminalCommandBuffer;

        if (!command.empty())
        {
            command.erase(command.begin(), std::find_if(command.begin(), command.end(), [](unsigned char character)
                                                        { return !std::isspace(character); }));
            command.erase(std::find_if(command.rbegin(), command.rend(), [](unsigned char character)
                                       { return !std::isspace(character); })
                              .base(),
                          command.end());
        }

        if (!command.empty())
        {
            m_forceTerminalScrollToBottom = true;

            if (command == "reload_shaders")
            {
                m_pendingShaderReloadRequest = true;
                VX_LOG(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal", "Queued shader reload request");
                m_notificationManager.showInfo("Shader reload queued");
            }
            else if (command == "compile_shaders")
            {
                std::vector<std::string> compileErrors;
                const size_t compiledShaders = engine::shaders::ShaderCompiler::compileDirectoryToSpv("./resources/shaders", &compileErrors);

                for (const auto &error : compileErrors)
                    VX_LOG(core::Logger::LogLayer::Developer, core::Logger::LogLevel::LOG_LEVEL_ERROR, "Terminal", error);

                if (compiledShaders > 0)
                {
                    m_pendingShaderReloadRequest = true;
                    VX_LOG_STREAM(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal",
                                  "Compiled " << compiledShaders << " shader source files. Reload queued.");
                    m_notificationManager.showSuccess("Shaders compiled");
                }
                else if (compileErrors.empty())
                {
                    VX_LOG(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal", "No shader source files were compiled.");
                    m_notificationManager.showInfo("No shader changes to compile");
                }
                else
                    m_notificationManager.showError("Shader compilation failed. Check terminal output.");
            }
            else
            {
                VX_LOG(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal", "$ " + command);

                const auto [executionResult, output] = FileHelper::executeCommand(command);

                if (!output.empty())
                {
                    std::stringstream outputStream(output);
                    std::string line;
                    while (std::getline(outputStream, line))
                    {
                        if (!line.empty())
                            VX_LOG(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal", line);
                    }
                }

                if (executionResult == 0)
                {
                    VX_LOG_STREAM(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal",
                                  "Command finished successfully. Exit code: " << executionResult);
                    m_notificationManager.showSuccess("Command executed successfully");
                }
                else
                {
                    VX_LOG_STREAM(core::Logger::LogLayer::Developer, core::Logger::LogLevel::LOG_LEVEL_ERROR, "Terminal",
                                  "Command failed. Exit code: " << executionResult);
                    m_notificationManager.showError("Command failed. Check terminal output.");
                }
            }
        }

        if (m_terminalClearInputOnSubmit)
            std::memset(m_terminalCommandBuffer, 0, sizeof(m_terminalCommandBuffer));
    }

    ImGui::End();
}

ELIX_NESTED_NAMESPACE_END
