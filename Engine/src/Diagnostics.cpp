#include "Engine/Diagnostics.hpp"
#include "Core/Logger.hpp"

#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace
{
    std::mutex g_diagnosticsMutex;
    std::filesystem::path g_logsDirectory;
    std::filesystem::path g_activeLogFilePath;
    std::atomic<bool> g_crashHandlerInstalled{false};
    std::atomic_flag g_crashReportWritten = ATOMIC_FLAG_INIT;

    std::tm makeLocalTime(std::time_t timeValue)
    {
        std::tm localTime{};
#if defined(_WIN32)
        localtime_s(&localTime, &timeValue);
#else
        localtime_r(&timeValue, &localTime);
#endif
        return localTime;
    }

    std::string buildTimestampString(bool fileNameSafe)
    {
        const auto now = std::chrono::system_clock::now();
        const auto timeValue = std::chrono::system_clock::to_time_t(now);
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        const std::tm localTime = makeLocalTime(timeValue);

        std::ostringstream stream;
        stream << std::put_time(&localTime, fileNameSafe ? "%Y-%m-%d_%H-%M-%S" : "%Y-%m-%d %H:%M:%S");
        stream << '.' << std::setfill('0') << std::setw(3) << milliseconds.count();
        return stream.str();
    }

    std::string sanitizeFileComponent(const std::string &value)
    {
        std::string result;
        result.reserve(value.size());

        for (unsigned char character : value)
        {
            if (std::isalnum(character) != 0)
                result.push_back(static_cast<char>(character));
            else
                result.push_back('_');
        }

        if (result.empty())
            return "velix";

        return result;
    }

    std::filesystem::path buildTimestampedPath(const std::filesystem::path &directory, const std::string &prefix, const std::string &extension)
    {
        return directory / (sanitizeFileComponent(prefix) + "_" + buildTimestampString(true) + extension);
    }

    std::string platformName()
    {
#if defined(_WIN32)
        return "Windows";
#elif defined(__linux__)
        return "Linux";
#else
        return "Unknown";
#endif
    }

    void emitCrashMessage(const std::string &message)
    {
        std::cerr << message << '\n';
#if defined(_WIN32)
        std::string withNewline = message + "\n";
        OutputDebugStringA(withNewline.c_str());
#endif
    }

    std::string signalNameFromValue(int signalValue)
    {
        switch (signalValue)
        {
        case SIGABRT:
            return "SIGABRT";
        case SIGFPE:
            return "SIGFPE";
        case SIGILL:
            return "SIGILL";
        case SIGINT:
            return "SIGINT";
        case SIGSEGV:
            return "SIGSEGV";
        case SIGTERM:
            return "SIGTERM";
#ifdef SIGBUS
        case SIGBUS:
            return "SIGBUS";
#endif
        default:
            return "UNKNOWN_SIGNAL";
        }
    }

    std::string describeCurrentException()
    {
        const std::exception_ptr currentException = std::current_exception();
        if (!currentException)
            return "No active exception information.";

        try
        {
            std::rethrow_exception(currentException);
        }
        catch (const std::exception &exception)
        {
            return std::string("std::exception: ") + exception.what();
        }
        catch (...)
        {
            return "Unknown non-std exception.";
        }
    }

    std::filesystem::path writeCrashReportInternal(const std::string &reason, const std::string &details, bool singleShot)
    {
        if (singleShot && g_crashReportWritten.test_and_set())
            return {};

        const std::filesystem::path logsDirectory = elix::engine::diagnostics::ensureLogsDirectory();
        if (logsDirectory.empty())
            return {};

        std::error_code errorCode;
        std::filesystem::create_directories(logsDirectory, errorCode);

        const std::filesystem::path crashFilePath = buildTimestampedPath(logsDirectory, "crash", ".txt");
        std::ofstream file(crashFilePath, std::ios::trunc);
        if (!file.is_open())
            return {};

        file << "Velix Crash Report\n";
        file << "Timestamp: " << buildTimestampString(false) << '\n';
        file << "Platform: " << platformName() << '\n';

        const std::filesystem::path executablePath = elix::engine::diagnostics::getExecutablePath();
        if (!executablePath.empty())
            file << "Executable: " << executablePath.string() << '\n';

        const std::filesystem::path activeLogFilePath = elix::engine::diagnostics::getActiveLogFilePath();
        if (!activeLogFilePath.empty())
            file << "Log File: " << activeLogFilePath.string() << '\n';

        file << "Reason: " << reason << '\n';
        if (!details.empty())
            file << "Details:\n"
                 << details << '\n';

        file.flush();
        return crashFilePath;
    }

    void terminateHandler()
    {
        const std::filesystem::path crashFilePath = writeCrashReportInternal("Unhandled terminate", describeCurrentException(), true);
        if (!crashFilePath.empty())
            emitCrashMessage("Crash report written to: " + crashFilePath.string());

        std::_Exit(EXIT_FAILURE);
    }

    void signalCrashHandler(int signalValue)
    {
        std::ostringstream details;
        details << "Signal: " << signalNameFromValue(signalValue) << " (" << signalValue << ')';

        const std::filesystem::path crashFilePath = writeCrashReportInternal("Fatal signal", details.str(), true);
        if (!crashFilePath.empty())
            emitCrashMessage("Crash report written to: " + crashFilePath.string());

        std::_Exit(128 + signalValue);
    }

#if defined(_WIN32)
    LONG WINAPI windowsUnhandledExceptionHandler(EXCEPTION_POINTERS *exceptionPointers)
    {
        std::ostringstream details;
        if (exceptionPointers && exceptionPointers->ExceptionRecord)
        {
            details << "Exception code: 0x" << std::hex << std::uppercase << exceptionPointers->ExceptionRecord->ExceptionCode << std::dec << '\n';
            details << "Exception address: " << exceptionPointers->ExceptionRecord->ExceptionAddress;
        }
        else
        {
            details << "No structured exception information available.";
        }

        const std::filesystem::path crashFilePath = writeCrashReportInternal("Unhandled structured exception", details.str(), true);
        if (!crashFilePath.empty())
            emitCrashMessage("Crash report written to: " + crashFilePath.string());

        return EXCEPTION_EXECUTE_HANDLER;
    }
#endif
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace diagnostics
{
    std::filesystem::path getExecutablePath()
    {
#if defined(_WIN32)
        std::wstring buffer(MAX_PATH, L'\0');

        while (true)
        {
            const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (size == 0)
                return {};

            if (size < buffer.size() - 1)
            {
                buffer.resize(size);
                return std::filesystem::path(buffer);
            }

            buffer.resize(buffer.size() * 2);
        }
#elif defined(__linux__)
        std::vector<char> buffer(1024, '\0');

        while (true)
        {
            const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size());
            if (size <= 0)
                return {};

            if (size < static_cast<ssize_t>(buffer.size()))
                return std::filesystem::path(std::string(buffer.data(), static_cast<size_t>(size)));

            buffer.resize(buffer.size() * 2, '\0');
        }
#else
        return {};
#endif
    }

    std::filesystem::path getExecutableDirectory()
    {
        const std::filesystem::path executablePath = getExecutablePath();
        return executablePath.empty() ? std::filesystem::path{} : executablePath.parent_path();
    }

    std::filesystem::path ensureLogsDirectory()
    {
        std::lock_guard<std::mutex> lock(g_diagnosticsMutex);

        if (g_logsDirectory.empty())
        {
            g_logsDirectory = getExecutableDirectory();
            if (g_logsDirectory.empty())
            {
                std::error_code currentPathError;
                g_logsDirectory = std::filesystem::current_path(currentPathError);
            }

            g_logsDirectory /= "logs";
        }

        std::error_code errorCode;
        std::filesystem::create_directories(g_logsDirectory, errorCode);
        if (errorCode)
            return {};

        return g_logsDirectory;
    }

    std::filesystem::path configureDefaultLogging(const std::string &baseFileName)
    {
        const std::filesystem::path logsDirectory = ensureLogsDirectory();
        if (logsDirectory.empty())
            return {};

        std::filesystem::path logFilePath;
        {
            std::lock_guard<std::mutex> lock(g_diagnosticsMutex);
            if (g_activeLogFilePath.empty())
                g_activeLogFilePath = buildTimestampedPath(logsDirectory, baseFileName.empty() ? "velix" : baseFileName, ".log");
            logFilePath = g_activeLogFilePath;
        }

        elix::core::Logger::createDefaultLogger();
        if (auto *logger = elix::core::Logger::getDefaultLogger())
            logger->setFileOutputPath(logFilePath.string());

        return logFilePath;
    }

    std::filesystem::path getActiveLogFilePath()
    {
        std::lock_guard<std::mutex> lock(g_diagnosticsMutex);
        return g_activeLogFilePath;
    }

    void installCrashHandler(const std::filesystem::path &logsDirectory)
    {
        if (!logsDirectory.empty())
        {
            std::lock_guard<std::mutex> lock(g_diagnosticsMutex);
            g_logsDirectory = logsDirectory;
        }

        ensureLogsDirectory();

        if (g_crashHandlerInstalled.exchange(true))
            return;

        std::set_terminate(terminateHandler);
        std::signal(SIGABRT, signalCrashHandler);
        std::signal(SIGFPE, signalCrashHandler);
        std::signal(SIGILL, signalCrashHandler);
        std::signal(SIGSEGV, signalCrashHandler);
#ifdef SIGBUS
        std::signal(SIGBUS, signalCrashHandler);
#endif

#if defined(_WIN32)
        SetUnhandledExceptionFilter(windowsUnhandledExceptionHandler);
#endif
    }

    std::filesystem::path writeCrashReport(const std::string &reason, const std::string &details)
    {
        return writeCrashReportInternal(reason, details, false);
    }
} // namespace diagnostics

ELIX_NESTED_NAMESPACE_END
