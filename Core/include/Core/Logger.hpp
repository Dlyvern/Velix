#ifndef ELIX_LOGGER_HPP
#define ELIX_LOGGER_HPP

#include "Core/Macros.hpp"

#include <iostream>
#include <string>
#include <mutex>
#include <cstdint>
#include <fstream>
#include <memory>
#include <deque>
#include <unordered_map>
#include <vector>
#include <functional>
#include <sstream>
#include <cstdlib>

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

    enum class LogLayer : uint8_t
    {
        Core = 0,
        Engine = 1,
        Editor = 2,
        Developer = 3,
        User = 4
    };

    struct LogMessage
    {
        std::string timestamp;
        LogLevel level{LogLevel::INFO};
        LogLayer layer{LogLayer::Developer};
        std::string category{"General"};
        std::string message;
        std::string formattedMessage;
    };

    using SinkId = uint64_t;
    using SinkCallback = std::function<void(const LogMessage &)>;

    Logger();

    explicit Logger(const std::string& logFilePath);

    void debug(const std::string &message, LogLayer layer = LogLayer::Developer, const std::string &category = "General");
    void info(const std::string &message, LogLayer layer = LogLayer::Developer, const std::string &category = "General");
    void error(const std::string &message, LogLayer layer = LogLayer::Developer, const std::string &category = "General");
    void warning(const std::string &message, LogLayer layer = LogLayer::Developer, const std::string &category = "General");

    void log(LogLevel logLevel, const std::string &message);
    void log(LogLevel logLevel, LogLayer layer, const std::string &category, const std::string &message);

    SinkId addSink(SinkCallback callback);
    void removeSink(SinkId sinkId);

    std::vector<LogMessage> getHistorySnapshot() const;
    void clearHistory();
    void setMaxHistory(size_t maxHistory);
    void setConsoleOutputEnabled(bool enabled);
    void setFileOutputPath(const std::string &logFilePath);

    static void setDefaultLogger(std::unique_ptr<Logger> logger);
    static void createDefaultLogger();
    static Logger* getDefaultLogger();

    static std::string logLevelToString(LogLevel level);
    static std::string logLayerToString(LogLayer layer);

    ~Logger();
private:
    static std::string getCurrentTime();
    static Logger::TerminalColorType logLevelToColor(LogLevel level);
    static std::string buildFormattedMessage(const LogMessage &logMessage);
    void pushHistory(const LogMessage &logMessage);

    mutable std::mutex m_logMutex;

    bool m_isConsoleOutput{true};
    bool m_isOutputFile{false};

    std::ofstream m_logFile;
    size_t m_maxHistory{4000};
    SinkId m_nextSinkId{1};

    std::deque<LogMessage> m_history;
    std::unordered_map<SinkId, SinkCallback> m_sinks;

    static inline std::unique_ptr<Logger> s_defaultLogger{nullptr};
};

#define VX_LOG(layer, level, category, message)                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        auto *__vx_logger = elix::core::Logger::getDefaultLogger();                                                   \
        if (__vx_logger)                                                                                                \
            __vx_logger->log(level, layer, category, message);                                                         \
    } while (0)

#define VX_LOG_STREAM(layer, level, category, expr)                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        std::ostringstream __vx_stream;                                                                                \
        __vx_stream << expr;                                                                                            \
        VX_LOG(layer, level, category, __vx_stream.str());                                                             \
    } while (0)

#define VX_CORE_INFO_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Core, elix::core::Logger::LogLevel::INFO, "Core", expr)
#define VX_CORE_WARNING_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Core, elix::core::Logger::LogLevel::WARNING, "Core", expr)
#define VX_CORE_ERROR_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Core, elix::core::Logger::LogLevel::LOG_LEVEL_ERROR, "Core", expr)
#define VX_CORE_DEBUG_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Core, elix::core::Logger::LogLevel::DEBUG, "Core", expr)

#define VX_ENGINE_INFO_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Engine, elix::core::Logger::LogLevel::INFO, "Engine", expr)
#define VX_ENGINE_WARNING_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Engine, elix::core::Logger::LogLevel::WARNING, "Engine", expr)
#define VX_ENGINE_ERROR_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Engine, elix::core::Logger::LogLevel::LOG_LEVEL_ERROR, "Engine", expr)
#define VX_ENGINE_DEBUG_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Engine, elix::core::Logger::LogLevel::DEBUG, "Engine", expr)

#define VX_EDITOR_INFO_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Editor, elix::core::Logger::LogLevel::INFO, "Editor", expr)
#define VX_EDITOR_WARNING_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Editor, elix::core::Logger::LogLevel::WARNING, "Editor", expr)
#define VX_EDITOR_ERROR_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Editor, elix::core::Logger::LogLevel::LOG_LEVEL_ERROR, "Editor", expr)
#define VX_EDITOR_DEBUG_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Editor, elix::core::Logger::LogLevel::DEBUG, "Editor", expr)

#define VX_DEV_INFO_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Developer, elix::core::Logger::LogLevel::INFO, "Developer", expr)
#define VX_DEV_WARNING_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Developer, elix::core::Logger::LogLevel::WARNING, "Developer", expr)
#define VX_DEV_ERROR_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Developer, elix::core::Logger::LogLevel::LOG_LEVEL_ERROR, "Developer", expr)
#define VX_DEV_DEBUG_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::Developer, elix::core::Logger::LogLevel::DEBUG, "Developer", expr)

#define VX_USER_INFO_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::User, elix::core::Logger::LogLevel::INFO, "User", expr)
#define VX_USER_WARNING_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::User, elix::core::Logger::LogLevel::WARNING, "User", expr)
#define VX_USER_ERROR_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::User, elix::core::Logger::LogLevel::LOG_LEVEL_ERROR, "User", expr)
#define VX_USER_DEBUG_STREAM(expr) VX_LOG_STREAM(elix::core::Logger::LogLayer::User, elix::core::Logger::LogLevel::DEBUG, "User", expr)

#define VX_INFO_STREAM(expr) VX_DEV_INFO_STREAM(expr)
#define VX_WARNING_STREAM(expr) VX_DEV_WARNING_STREAM(expr)
#define VX_ERROR_STREAM(expr) VX_DEV_ERROR_STREAM(expr)
#define VX_DEBUG_STREAM(expr) VX_DEV_DEBUG_STREAM(expr)

#define VX_DEBUG(message) VX_LOG(elix::core::Logger::LogLayer::Developer, elix::core::Logger::LogLevel::DEBUG, "General", message)
#define VX_INFO(message) VX_LOG(elix::core::Logger::LogLayer::Developer, elix::core::Logger::LogLevel::INFO, "General", message)
#define VX_WARNING(message) VX_LOG(elix::core::Logger::LogLayer::Developer, elix::core::Logger::LogLevel::WARNING, "General", message)
#define VX_ERROR(message) VX_LOG(elix::core::Logger::LogLayer::Developer, elix::core::Logger::LogLevel::LOG_LEVEL_ERROR, "General", message)

#define VX_FATAL(msg) \
    do { \
        VX_ERROR(msg); \
        std::abort(); \
    } while (0)

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_LOGGER_HPP
