#include "Editor/Panels/TerminalPanel.hpp"

#include "Editor/FileHelper.hpp"
#include "Core/Logger.hpp"
#include "Engine/Shaders/ShaderCompiler.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <utility>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

namespace
{
    std::string normalizeTerminalCommand(std::string command)
    {
        command.erase(command.begin(), std::find_if(command.begin(), command.end(), [](unsigned char character)
                                                    { return !std::isspace(character); }));
        command.erase(std::find_if(command.rbegin(), command.rend(), [](unsigned char character)
                                   { return !std::isspace(character); })
                          .base(),
                      command.end());

        std::string normalized;
        normalized.reserve(command.size());

        bool previousWasSpace = false;
        for (unsigned char character : command)
        {
            if (std::isspace(character))
            {
                if (!normalized.empty() && !previousWasSpace)
                    normalized.push_back(' ');

                previousWasSpace = true;
                continue;
            }

            normalized.push_back(static_cast<char>(std::tolower(character)));
            previousWasSpace = false;
        }

        if (!normalized.empty() && normalized.back() == ' ')
            normalized.pop_back();

        return normalized;
    }

} // namespace

void TerminalPanel::addFunction(const std::string &command, const std::function<void()> &function)
{
    const std::string normalizedCommand = normalizeTerminalCommand(command);
    if (normalizedCommand.empty() || !function)
        return;

    auto existingFunctionIt = std::find_if(m_registeredFunctions.begin(), m_registeredFunctions.end(),
                                           [&](const RegisteredFunction &registeredFunction)
                                           { return registeredFunction.command == normalizedCommand; });

    if (existingFunctionIt != m_registeredFunctions.end())
    {
        existingFunctionIt->function = function;
        return;
    }

    m_registeredFunctions.push_back({normalizedCommand, function});
}

void TerminalPanel::setNotificationManager(NotificationManager *notificationManager)
{
    m_notificationManager = notificationManager;
}

void TerminalPanel::setQueueShaderReloadRequestCallback(const std::function<void()> &function)
{
    m_queueShaderReloadRequest = function;
}

void TerminalPanel::notify(NotificationType type, const std::string &message)
{
    if (!m_notificationManager)
        return;

    switch (type)
    {
    case NotificationType::Info:
        m_notificationManager->showInfo(message);
        break;
    case NotificationType::Success:
        m_notificationManager->showSuccess(message);
        break;
    case NotificationType::Warning:
        m_notificationManager->showWarning(message);
        break;
    case NotificationType::Error:
        m_notificationManager->showError(message);
        break;
    }
}

void TerminalPanel::queueShaderReloadRequest()
{
    if (m_queueShaderReloadRequest)
        m_queueShaderReloadRequest();
}

bool TerminalPanel::executeRegisteredFunction(const std::string &command)
{
    const std::string normalizedCommand = normalizeTerminalCommand(command);
    if (normalizedCommand.empty())
        return false;

    auto functionIt = std::find_if(m_registeredFunctions.begin(), m_registeredFunctions.end(),
                                   [&](const RegisteredFunction &registeredFunction)
                                   { return registeredFunction.command == normalizedCommand; });
    if (functionIt == m_registeredFunctions.end() || !functionIt->function)
        return false;

    VX_LOG(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal", "$ " + normalizedCommand);
    functionIt->function();
    return true;
}

void TerminalPanel::draw(bool *open)
{
    if (!open || !*open)
        return;

    ImGui::Begin("Terminal with logs", open);

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
        m_lastLogCount = 0;
        m_forceScrollToBottom = true;
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("Auto-scroll", &m_autoScroll) && m_autoScroll)
        m_forceScrollToBottom = true;

    ImGui::SameLine();
    ImGui::Checkbox("Clear input on run", &m_clearInputOnSubmit);

    auto drawLayerToggle = [this](const char *label, int bit)
    {
        bool enabled = (m_selectedLayerMask & (1 << bit)) != 0;
        if (ImGui::Checkbox(label, &enabled))
        {
            if (enabled)
                m_selectedLayerMask |= (1 << bit);
            else
                m_selectedLayerMask &= ~(1 << bit);
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
        const bool hasNewLogs = currentLogCount > m_lastLogCount;
        const bool historyShrank = currentLogCount < m_lastLogCount;

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

            if ((m_selectedLayerMask & (1 << layerBit)) == 0)
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

        const bool shouldAutoScroll = m_autoScroll &&
                                      (m_forceScrollToBottom || historyShrank || (hasNewLogs && wasAtBottom));

        if (shouldAutoScroll)
            ImGui::SetScrollHereY(1.0f);

        m_lastLogCount = currentLogCount;
        m_forceScrollToBottom = false;
    }
    ImGui::EndChild();

    ImGui::PushItemWidth(-70.0f);
    const bool submitWithEnter = ImGui::InputText("##TerminalCommand", m_commandBuffer.data(), m_commandBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();

    ImGui::SameLine();
    const bool submitWithButton = ImGui::Button("Run");

    if (submitWithEnter || submitWithButton)
    {
        const std::string command = normalizeTerminalCommand(m_commandBuffer.data());

        if (!command.empty())
        {
            m_forceScrollToBottom = true;

            if (executeRegisteredFunction(command))
            {
            }
            else if (command == "reload_shaders")
            {
                queueShaderReloadRequest();
                VX_LOG(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal", "Queued shader reload request");
                notify(NotificationType::Info, "Shader reload queued");
            }
            else if (command == "compile_shaders")
            {
                std::vector<std::string> compileErrors;
                const size_t compiledShaders = engine::shaders::ShaderCompiler::compileDirectoryToSpv("./resources/shaders", &compileErrors);

                for (const auto &error : compileErrors)
                    VX_LOG(core::Logger::LogLayer::Developer, core::Logger::LogLevel::LOG_LEVEL_ERROR, "Terminal", error);

                if (compiledShaders > 0)
                {
                    queueShaderReloadRequest();
                    VX_LOG_STREAM(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal",
                                  "Compiled " << compiledShaders << " shader source files. Reload queued.");
                    notify(NotificationType::Success, "Shaders compiled");
                }
                else if (compileErrors.empty())
                {
                    VX_LOG(core::Logger::LogLayer::Developer, core::Logger::LogLevel::INFO, "Terminal", "No shader source files were compiled.");
                    notify(NotificationType::Info, "No shader changes to compile");
                }
                else
                    notify(NotificationType::Error, "Shader compilation failed. Check terminal output.");
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
                    notify(NotificationType::Success, "Command executed successfully");
                }
                else
                {
                    VX_LOG_STREAM(core::Logger::LogLayer::Developer, core::Logger::LogLevel::LOG_LEVEL_ERROR, "Terminal",
                                  "Command failed. Exit code: " << executionResult);
                    notify(NotificationType::Error, "Command failed. Check terminal output.");
                }
            }
        }

        if (m_clearInputOnSubmit)
            std::memset(m_commandBuffer.data(), 0, m_commandBuffer.size());
    }

    ImGui::End();
}

ELIX_NESTED_NAMESPACE_END
