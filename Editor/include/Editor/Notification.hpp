#ifndef ELIX_NOTIFICATION_HPP
#define ELIX_NOTIFICATION_HPP

#include "Core/Macros.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

enum class NotificationType
{
    Info,
    Success,
    Warning,
    Error
};

struct Notification
{
    std::string message;
    NotificationType type;
    std::chrono::steady_clock::time_point startTime;
    float duration; // in seconds
    bool fadeOut;

    Notification(const std::string &msg, NotificationType t, float dur = 3.0f, bool fade = true)
        : message(msg), type(t), duration(dur), fadeOut(fade)
    {
        startTime = std::chrono::steady_clock::now();
    }

    float getRemainingTime() const
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<float>(now - startTime).count();
        return std::max(0.0f, duration - elapsed);
    }

    bool isExpired() const
    {
        return getRemainingTime() <= 0.0f;
    }

    float getAlpha() const
    {
        if (!fadeOut)
            return 1.0f;

        float remaining = getRemainingTime();

        if (remaining <= 0.5f)
            return remaining / 0.5f; // Fade out over last 0.5 seconds

        return 1.0f;
    }
};

class NotificationManager
{
private:
    std::vector<Notification> m_notifications;
    ImVec2 m_position;
    float m_width;
    float m_padding;

public:
    NotificationManager()
    {
        m_position = ImVec2(10, 20);
        m_width = 300.0f;
        m_padding = 10.0f;
    }

    void setPosition(ImVec2 pos) { m_position = pos; }
    void setWidth(float width) { m_width = width; }

    void show(const std::string &message, NotificationType type = NotificationType::Info, float duration = 3.0f)
    {
        m_notifications.emplace_back(message, type, duration);

        if (m_notifications.size() > 10)
        {
            m_notifications.erase(m_notifications.begin());
        }
    }

    void showInfo(const std::string &message, float duration = 3.0f)
    {
        show(message, NotificationType::Info, duration);
    }

    void showSuccess(const std::string &message, float duration = 3.0f)
    {
        show(message, NotificationType::Success, duration);
    }

    void showWarning(const std::string &message, float duration = 3.0f)
    {
        show(message, NotificationType::Warning, duration);
    }

    void showError(const std::string &message, float duration = 3.0f)
    {
        show(message, NotificationType::Error, duration);
    }

    void render()
    {
        if (m_notifications.empty())
            return;

        m_notifications.erase(
            std::remove_if(m_notifications.begin(), m_notifications.end(),
                           [](const Notification &n)
                           { return n.isExpired(); }),
            m_notifications.end());

        ImVec2 displaySize = ImGui::GetIO().DisplaySize;

        float startY = displaySize.y - m_position.y;

        for (auto it = m_notifications.rbegin(); it != m_notifications.rend(); ++it)
        {
            Notification &notification = *it;

            ImVec2 notificationPos(
                displaySize.x - m_width - m_position.x,
                startY - m_padding);

            ImGui::SetNextWindowPos(notificationPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(m_width, 0), ImGuiCond_Always);

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoFocusOnAppearing |
                                     ImGuiWindowFlags_NoNav;

            float alpha = notification.getAlpha();
            if (alpha <= 0.0f)
                continue;

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

            switch (notification.type)
            {
            case NotificationType::Info:
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.3f, 0.9f));
                break;
            case NotificationType::Success:
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.3f, 0.1f, 0.9f));
                break;
            case NotificationType::Warning:
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.3f, 0.2f, 0.1f, 0.9f));
                break;
            case NotificationType::Error:
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.3f, 0.1f, 0.1f, 0.9f));
                break;
            }

            std::string windowName = "##notification_" + std::to_string(reinterpret_cast<uintptr_t>(&notification));
            ImGui::Begin(windowName.c_str(), nullptr, flags);

            switch (notification.type)
            {
            case NotificationType::Info:
                ImGui::Text("I");
                ImGui::SameLine();
                break;
            case NotificationType::Success:
                ImGui::Text("@");
                ImGui::SameLine();
                break;
            case NotificationType::Warning:
                ImGui::Text("^");
                ImGui::SameLine();
                break;
            case NotificationType::Error:
                ImGui::Text("X");
                ImGui::SameLine();
                break;
            }

            ImGui::PushTextWrapPos(m_width - 40.0f);
            ImGui::Text("%s", notification.message.c_str());
            ImGui::PopTextWrapPos();

            ImGui::SameLine(ImGui::GetWindowWidth() - 25.0f);

            if (ImGui::SmallButton("x"))
                notification.duration = 0.0f;

            ImGui::End();

            ImGui::PopStyleColor();

            ImGui::PopStyleVar();

            startY -= (ImGui::GetWindowHeight() + m_padding);
        }
    }
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_NOTIFICATION_HPP
