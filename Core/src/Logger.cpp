#include "Core/Logger.hpp"
#include <chrono>
#include <sstream>
#include <iomanip>

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

    inline void logColoredMessageToTerminal(elix::core::Logger::TerminalColorType color, const std::string& message)
    {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, color);

        std::cout << message << '\n';
    }
#else
    const elix::core::Logger::TerminalColorType RED = "\033[31m";
    const elix::core::Logger::TerminalColorType GREEN = "\033[32m";
    const elix::core::Logger::TerminalColorType YELLOW = "\033[33m";
    const elix::core::Logger::TerminalColorType WHITE = "\033[37m";
    const elix::core::Logger::TerminalColorType BLUE = "\033[34m";

    inline void logColoredMessageToTerminal(const elix::core::Logger::TerminalColorType& color, const std::string& message)
    {
        std::cout << color << message << "\033[0m" << '\n';
    }
#endif
} // namespace terminalColors


ELIX_NESTED_NAMESPACE_BEGIN(core)

Logger::Logger() = default;

Logger::Logger(const std::string& logFilePath)
{
    if(!logFilePath.empty())
    {
        m_logFile.open(logFilePath, std::ios::app);
        m_isOutputFile = m_logFile.is_open();
    }
}

void Logger::setDefaultLogger(std::unique_ptr<Logger> logger)
{
    s_defaultLogger = std::move(logger);
}

void Logger::createDefaultLogger()
{
    s_defaultLogger = std::make_unique<Logger>();
}

Logger* Logger::getDefaultLogger()
{
    return s_defaultLogger.get();
}

void Logger::debug(const std::string& message)
{
// #if DEBUG_MODE
    log(LogLevel::DEBUG, message);
// #endif 
}

void Logger::info(const std::string& message)
{
    log(LogLevel::INFO, message);
}

void Logger::error(const std::string& message)
{
    log(LogLevel::LOG_LEVEL_ERROR, message);
}

void Logger::warning(const std::string& message)
{
    log(LogLevel::WARNING, message);
}

void Logger::log(LogLevel logLevel, const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_logMutex);
    
    std::string timestamp = getCurrentTime();
    std::string levelStr = logLevelToString(logLevel);

    Logger::TerminalColorType color = logLevelToColor(logLevel);
    
    std::stringstream logEntry;

    logEntry << "[" << timestamp << "] "
                << "[" << levelStr << "] "
                << message;

    if(m_isConsoleOutput)
        terminalColors::logColoredMessageToTerminal(color, logEntry.str());
    
    if(m_isOutputFile)
        m_logFile << logEntry.str() << '\n';
}

std::string Logger::getCurrentTime()
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

Logger::TerminalColorType Logger::logLevelToColor(LogLevel level)
{
    switch (level)
    {
        case LogLevel::DEBUG: return terminalColors::BLUE;
        case LogLevel::INFO: return terminalColors::GREEN;
        case LogLevel::WARNING: return terminalColors::YELLOW;
        case LogLevel::LOG_LEVEL_ERROR: return terminalColors::RED;
        default: return terminalColors::WHITE;
    }
}

std::string Logger::logLevelToString(LogLevel level)
{
    switch (level)
    {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::LOG_LEVEL_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

Logger::~Logger()
{
    if(m_logFile.is_open())
        m_logFile.close();
}

ELIX_NESTED_NAMESPACE_END