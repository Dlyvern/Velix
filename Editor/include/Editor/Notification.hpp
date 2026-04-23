#ifndef ELIX_NOTIFICATION_HPP
#define ELIX_NOTIFICATION_HPP

#include "Core/Macros.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"
#include "Editor/IconsLucide.hpp"

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
    float duration;
    float totalDuration;
    bool fadeOut;

    Notification(const std::string &msg, NotificationType t, float dur = 3.0f, bool fade = true)
        : message(msg), type(t), duration(dur), totalDuration(dur), fadeOut(fade)
    {
        startTime = std::chrono::steady_clock::now();
    }

    float getRemainingTime() const
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<float>(now - startTime).count();
        return std::max(0.0f, duration - elapsed);
    }

    float getProgress() const
    {
        if (totalDuration <= 0.0f)
            return 0.0f;
        return std::clamp(getRemainingTime() / totalDuration, 0.0f, 1.0f);
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
            return remaining / 0.5f;

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

        if (m_notifications.empty())
            return;

        ImGuiViewport *viewport = ImGui::GetMainViewport();
        const float rightEdge = viewport->Pos.x + viewport->Size.x - m_position.x;
        float bottomEdge = viewport->Pos.y + viewport->Size.y - m_position.y;

        for (auto it = m_notifications.rbegin(); it != m_notifications.rend(); ++it)
        {
            Notification &notification = *it;



            ImVec2 notificationPos(
                rightEdge - m_width,
                bottomEdge - m_padding);

            ImGui::SetNextWindowPos(notificationPos, ImGuiCond_Always, ImVec2(0.0f, 1.0f));
            ImGui::SetNextWindowSize(ImVec2(m_width, 0), ImGuiCond_Always);
            ImGui::SetNextWindowViewport(viewport->ID);

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoDocking |
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

            const char *icon = ICON_LC_Info;
            ImVec4 iconColor(0.247f, 0.692f, 0.917f, 1.0f);
            switch (notification.type)
            {
            case NotificationType::Info:
                icon = ICON_LC_Info;
                iconColor = ImVec4(0.247f, 0.692f, 0.917f, 1.0f);
                break;
            case NotificationType::Success:
                icon = ICON_LC_Check;
                iconColor = ImVec4(0.357f, 0.740f, 0.456f, 1.0f);
                break;
            case NotificationType::Warning:
                icon = ICON_LC_AlertTriangle;
                iconColor = ImVec4(0.916f, 0.709f, 0.195f, 1.0f);
                break;
            case NotificationType::Error:
                icon = ICON_LC_AlertOctagon;
                iconColor = ImVec4(0.987f, 0.345f, 0.334f, 1.0f);
                break;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, iconColor);
            ImGui::TextUnformatted(icon);
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::PushTextWrapPos(m_width - 40.0f);
            ImGui::Text("%s", notification.message.c_str());
            ImGui::PopTextWrapPos();

            ImGui::SameLine(ImGui::GetWindowWidth() - 25.0f);

            if (ImGui::SmallButton(ICON_LC_X))
                notification.duration = 0.0f;




            const ImVec2 winPos = ImGui::GetWindowPos();
            const ImVec2 winSize = ImGui::GetWindowSize();
            const float progress = notification.getProgress();
            const float barH = 3.0f;
            const float barWidth = winSize.x * progress;
            const float barY = winPos.y + winSize.y - barH;
            const ImU32 barCol = IM_COL32(239, 103, 90, 160);
            ImGui::GetForegroundDrawList()->AddRectFilled(
                ImVec2(winPos.x, barY),
                ImVec2(winPos.x + barWidth, barY + barH),
                barCol);

            const float notificationHeight = ImGui::GetWindowSize().y;
            ImGui::End();

            ImGui::PopStyleColor();

            ImGui::PopStyleVar();

            bottomEdge -= (notificationHeight + m_padding);
        }
    }
};

ELIX_NESTED_NAMESPACE_END

#endif
