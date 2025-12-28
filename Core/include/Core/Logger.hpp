#ifndef ELIX_LOGGER_HPP
#define ELIX_LOGGER_HPP

#include "Core/Macros.hpp"

#include <iostream>
#include <string>
#include <mutex>
#include <cstdint>
#include <fstream>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class Logger
{
public:
#ifdef _WIN32
    using TerminalColorType = int;
#else
    using TerminalColorType = std::string;
#endif

    enum class LogLevel : uint8_t
    {
        DEBUG = 0,
        INFO = 1,
        WARNING = 2,
        LOG_LEVEL_ERROR = 3,
    };

    Logger();

    explicit Logger(const std::string& logFilePath);

    void debug(const std::string& message);
    void info(const std::string& message);
    void error(const std::string& message);
    void warning(const std::string& message);

    void log(LogLevel logLevel, const std::string& message);

    static void setDefaultLogger(std::unique_ptr<Logger> logger);
    static void createDefaultLogger();
    static Logger* getDefaultLogger();

    ~Logger();
private:
    static std::string getCurrentTime();
    static std::string logLevelToString(LogLevel level);
    static Logger::TerminalColorType logLevelToColor(LogLevel level);

    std::mutex m_logMutex;

    bool m_isConsoleOutput{true};
    bool m_isOutputFile{false};

    std::ofstream m_logFile;

    static inline std::unique_ptr<Logger> s_defaultLogger{nullptr};
};

#define VX_DEBUG(message) elix::core::Logger::getDefaultLogger()->debug(message)
#define VX_INFO(message) elix::core::Logger::getDefaultLogger()->info(message)
#define VX_WARNING(message) elix::core::Logger::getDefaultLogger()->warning(message)
#define VX_ERROR(message) elix::core::Logger::getDefaultLogger()->error(message)

#define VX_FATAL(msg) \
    do { \
        VX_ERROR(msg); \
        std::abort(); \
    } while (0)

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_LOGGER_HPP