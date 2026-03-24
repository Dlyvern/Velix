#ifndef ELIX_TERMINAL_PANEL_HPP
#define ELIX_TERMINAL_PANEL_HPP

#include "Core/Macros.hpp"

#include "Editor/Notification.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class TerminalPanel
{
public:
    void draw(bool *open);

    void addFunction(const std::string &command, const std::function<void()> &function);
    void setNotificationManager(NotificationManager *notificationManager);
    void setQueueShaderReloadRequestCallback(const std::function<void()> &function);

private:
    struct RegisteredFunction
    {
        std::string command;
        std::function<void()> function;
    };

    void notify(NotificationType type, const std::string &message);
    void queueShaderReloadRequest();
    bool executeRegisteredFunction(const std::string &command);

    NotificationManager *m_notificationManager{nullptr};
    std::function<void()> m_queueShaderReloadRequest{nullptr};
    std::vector<RegisteredFunction> m_registeredFunctions;

    bool m_autoScroll{true};
    bool m_clearInputOnSubmit{true};
    std::array<char, 512> m_commandBuffer{};
    int m_selectedLayerMask{(1 << 5) - 1};
    std::size_t m_lastLogCount{0};
    bool m_forceScrollToBottom{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_TERMINAL_PANEL_HPP
