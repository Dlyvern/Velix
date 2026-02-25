#include "Core/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace terminalColors
{
#ifdef _WIN32
const elix::core::Logger::TerminalColorType RED = FOREGROUND_RED | FOREGROUND_INTENSITY;
const elix::core::Logger::TerminalColorType GREEN = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
const elix::core::Logger::TerminalColorType YELLOW = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
const elix::core::Logger::TerminalColorType WHITE = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
const elix::core::Logger::TerminalColorType BLUE = FOREGROUND_BLUE | FOREGROUND_INTENSITY;

inline void logColoredMessageToTerminal(elix::core::Logger::TerminalColorType color, const std::string &message)
{
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
    std::cout << message << '\n';
    SetConsoleTextAttribute(hConsole, WHITE);
}
#else
const elix::core::Logger::TerminalColorType RED = "\033[31m";
const elix::core::Logger::TerminalColorType GREEN = "\033[32m";
const elix::core::Logger::TerminalColorType YELLOW = "\033[33m";
const elix::core::Logger::TerminalColorType WHITE = "\033[37m";
const elix::core::Logger::TerminalColorType BLUE = "\033[34m";

inline void logColoredMessageToTerminal(const elix::core::Logger::TerminalColorType &color, const std::string &message)
{
    std::cout << color << message << "\033[0m" << '\n';
}
#endif
} // namespace terminalColors

ELIX_NESTED_NAMESPACE_BEGIN(core)

Logger::Logger() = default;

Logger::Logger(const std::string &logFilePath)
{
    setFileOutputPath(logFilePath);
}

void Logger::setDefaultLogger(std::unique_ptr<Logger> logger)
{
    s_defaultLogger = std::move(logger);
}

void Logger::createDefaultLogger()
{
    if (!s_defaultLogger)
        s_defaultLogger = std::make_unique<Logger>();
}

Logger *Logger::getDefaultLogger()
{
    return s_defaultLogger.get();
}

void Logger::debug(const std::string &message, LogLayer layer, const std::string &category)
{
    log(LogLevel::DEBUG, layer, category, message);
}

void Logger::info(const std::string &message, LogLayer layer, const std::string &category)
{
    log(LogLevel::INFO, layer, category, message);
}

void Logger::error(const std::string &message, LogLayer layer, const std::string &category)
{
    log(LogLevel::LOG_LEVEL_ERROR, layer, category, message);
}

void Logger::warning(const std::string &message, LogLayer layer, const std::string &category)
{
    log(LogLevel::WARNING, layer, category, message);
}

void Logger::log(LogLevel logLevel, const std::string &message)
{
    log(logLevel, LogLayer::Developer, "General", message);
}

void Logger::log(LogLevel logLevel, LogLayer layer, const std::string &category, const std::string &message)
{
    LogMessage logMessage{};
    logMessage.timestamp = getCurrentTime();
    logMessage.level = logLevel;
    logMessage.layer = layer;
    logMessage.category = category.empty() ? "General" : category;
    logMessage.message = message;
    logMessage.formattedMessage = buildFormattedMessage(logMessage);

    std::vector<SinkCallback> sinks;
    {
        std::lock_guard<std::mutex> lock(m_logMutex);

        if (m_isConsoleOutput)
            terminalColors::logColoredMessageToTerminal(logLevelToColor(logLevel), logMessage.formattedMessage);

        if (m_isOutputFile && m_logFile.is_open())
            m_logFile << logMessage.formattedMessage << '\n';

        pushHistory(logMessage);

        sinks.reserve(m_sinks.size());
        for (const auto &[_, callback] : m_sinks)
        {
            if (callback)
                sinks.push_back(callback);
        }
    }

    for (const auto &callback : sinks)
        callback(logMessage);
}

Logger::SinkId Logger::addSink(SinkCallback callback)
{
    if (!callback)
        return 0;

    std::lock_guard<std::mutex> lock(m_logMutex);
    const SinkId sinkId = m_nextSinkId++;
    m_sinks[sinkId] = std::move(callback);
    return sinkId;
}

void Logger::removeSink(SinkId sinkId)
{
    std::lock_guard<std::mutex> lock(m_logMutex);
    m_sinks.erase(sinkId);
}

std::vector<Logger::LogMessage> Logger::getHistorySnapshot() const
{
    std::lock_guard<std::mutex> lock(m_logMutex);
    return {m_history.begin(), m_history.end()};
}

void Logger::clearHistory()
{
    std::lock_guard<std::mutex> lock(m_logMutex);
    m_history.clear();
}

void Logger::setMaxHistory(size_t maxHistory)
{
    std::lock_guard<std::mutex> lock(m_logMutex);
    m_maxHistory = std::max<size_t>(maxHistory, 1);

    while (m_history.size() > m_maxHistory)
        m_history.pop_front();
}

void Logger::setConsoleOutputEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(m_logMutex);
    m_isConsoleOutput = enabled;
}

void Logger::setFileOutputPath(const std::string &logFilePath)
{
    std::lock_guard<std::mutex> lock(m_logMutex);

    if (m_logFile.is_open())
        m_logFile.close();

    m_isOutputFile = false;

    if (logFilePath.empty())
        return;

    m_logFile.open(logFilePath, std::ios::app);
    m_isOutputFile = m_logFile.is_open();
}

std::string Logger::getCurrentTime()
{
    const auto now = std::chrono::system_clock::now();
    const auto time_t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

Logger::TerminalColorType Logger::logLevelToColor(LogLevel level)
{
    switch (level)
    {
    case LogLevel::DEBUG:
        return terminalColors::BLUE;
    case LogLevel::INFO:
        return terminalColors::GREEN;
    case LogLevel::WARNING:
        return terminalColors::YELLOW;
    case LogLevel::LOG_LEVEL_ERROR:
        return terminalColors::RED;
    default:
        return terminalColors::WHITE;
    }
}

std::string Logger::logLevelToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARNING:
        return "WARNING";
    case LogLevel::LOG_LEVEL_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

std::string Logger::logLayerToString(LogLayer layer)
{
    switch (layer)
    {
    case LogLayer::Core:
        return "Core";
    case LogLayer::Engine:
        return "Engine";
    case LogLayer::Editor:
        return "Editor";
    case LogLayer::Developer:
        return "Developer";
    case LogLayer::User:
        return "User";
    default:
        return "Unknown";
    }
}

std::string Logger::buildFormattedMessage(const LogMessage &logMessage)
{
    std::stringstream entry;
    entry << "[" << logMessage.timestamp << "]"
          << "[" << logLevelToString(logMessage.level) << "]"
          << "[" << logLayerToString(logMessage.layer) << "]"
          << "[" << logMessage.category << "] "
          << logMessage.message;

    return entry.str();
}

void Logger::pushHistory(const LogMessage &logMessage)
{
    m_history.push_back(logMessage);

    while (m_history.size() > m_maxHistory)
        m_history.pop_front();
}

Logger::~Logger()
{
    if (m_logFile.is_open())
        m_logFile.close();
}

ELIX_NESTED_NAMESPACE_END
