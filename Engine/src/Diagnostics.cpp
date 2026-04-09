#include "Engine/Diagnostics.hpp"
#include "Core/Logger.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dbghelp.h>
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#endif
#include <unistd.h>
#endif

namespace
{
    std::mutex g_diagnosticsMutex;
    std::filesystem::path g_logsDirectory;
    std::filesystem::path g_activeLogFilePath;
    std::atomic<bool> g_crashHandlerInstalled{false};
    std::atomic_flag g_crashReportWritten = ATOMIC_FLAG_INIT;
    std::once_flag g_stackTraceSymbolsInitFlag;

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

    std::string pointerToString(const void *pointer)
    {
        if (!pointer)
            return "0x0";

        std::ostringstream stream;
        stream << "0x" << std::hex << std::uppercase << reinterpret_cast<uintptr_t>(pointer);
        return stream.str();
    }

    uint64_t currentProcessId()
    {
#if defined(_WIN32)
        return static_cast<uint64_t>(GetCurrentProcessId());
#elif defined(__linux__) || defined(__APPLE__)
        return static_cast<uint64_t>(getpid());
#else
        return 0ull;
#endif
    }

    std::string currentThreadIdString()
    {
        std::ostringstream stream;
        stream << std::this_thread::get_id();
        return stream.str();
    }

#if defined(_WIN32)
    void ensureWindowsStackTraceSymbolsInitialized()
    {
        std::call_once(g_stackTraceSymbolsInitFlag, []()
                       { SymInitialize(GetCurrentProcess(), nullptr, TRUE); });
    }

    std::string captureCurrentStackTrace(size_t skipFrames = 0, size_t maxFrames = 64)
    {
        ensureWindowsStackTraceSymbolsInitialized();

        constexpr size_t kMaxFrames = 128;
        void *frames[kMaxFrames]{};
        const USHORT frameCount = CaptureStackBackTrace(
            static_cast<DWORD>(skipFrames + 1),
            static_cast<DWORD>(std::min(maxFrames, kMaxFrames)),
            frames,
            nullptr);

        if (frameCount == 0)
            return "  <stack trace unavailable>";

        HANDLE process = GetCurrentProcess();
        std::ostringstream stream;

        constexpr DWORD kMaxSymbolNameLength = 512;
        alignas(SYMBOL_INFO) char symbolStorage[sizeof(SYMBOL_INFO) + kMaxSymbolNameLength];
        auto *symbolInfo = reinterpret_cast<SYMBOL_INFO *>(symbolStorage);
        symbolInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbolInfo->MaxNameLen = kMaxSymbolNameLength;

        IMAGEHLP_LINE64 lineInfo{};
        lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

        for (USHORT frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            const auto address = reinterpret_cast<DWORD64>(frames[frameIndex]);
            DWORD64 displacement = 0;
            DWORD lineDisplacement = 0;

            stream << "  [" << frameIndex << "] ";

            if (SymFromAddr(process, address, &displacement, symbolInfo))
            {
                stream << symbolInfo->Name;
                if (displacement != 0)
                    stream << " +0x" << std::hex << displacement << std::dec;
            }
            else
            {
                stream << pointerToString(frames[frameIndex]);
            }

            if (SymGetLineFromAddr64(process, address, &lineDisplacement, &lineInfo))
                stream << " (" << lineInfo.FileName << ':' << lineInfo.LineNumber << ')';

            stream << '\n';
        }

        return stream.str();
    }
#elif defined(__linux__) || defined(__APPLE__)
    std::string demangleBacktraceLine(const char *rawLine)
    {
        if (!rawLine)
            return "<unknown>";

        std::string line(rawLine);

#if defined(__GNUC__) || defined(__clang__)
        const size_t mangledBegin = line.find('(');
        const size_t mangledEnd = line.find('+', mangledBegin == std::string::npos ? 0 : mangledBegin);
        if (mangledBegin != std::string::npos && mangledEnd != std::string::npos && mangledEnd > mangledBegin + 1)
        {
            const std::string mangledName = line.substr(mangledBegin + 1, mangledEnd - mangledBegin - 1);
            int status = 0;
            char *demangledName = abi::__cxa_demangle(mangledName.c_str(), nullptr, nullptr, &status);
            if (status == 0 && demangledName)
            {
                line.replace(mangledBegin + 1, mangledEnd - mangledBegin - 1, demangledName);
                std::free(demangledName);
            }
            else if (demangledName)
            {
                std::free(demangledName);
            }
        }
#endif

        return line;
    }

    std::string captureCurrentStackTrace(size_t skipFrames = 0, size_t maxFrames = 64)
    {
        constexpr size_t kMaxFrames = 128;
        void *frames[kMaxFrames]{};
        const int frameCount = backtrace(frames, static_cast<int>(std::min(maxFrames + skipFrames + 1, kMaxFrames)));
        if (frameCount <= 0)
            return "  <stack trace unavailable>";

        char **symbols = backtrace_symbols(frames, frameCount);
        std::ostringstream stream;

        for (int frameIndex = static_cast<int>(skipFrames + 1); frameIndex < frameCount; ++frameIndex)
        {
            stream << "  [" << (frameIndex - static_cast<int>(skipFrames + 1)) << "] ";

            if (symbols)
                stream << demangleBacktraceLine(symbols[frameIndex]);
            else
                stream << pointerToString(frames[frameIndex]);

            stream << '\n';
        }

        if (symbols)
            std::free(symbols);

        return stream.str();
    }
#else
    std::string captureCurrentStackTrace(size_t = 0, size_t = 64)
    {
        return "  <stack trace capture is not supported on this platform>";
    }
#endif

    std::string buildCrashContextSummary()
    {
        std::ostringstream details;
        details << "Process ID: " << currentProcessId() << '\n';
        details << "Thread ID: " << currentThreadIdString();
        return details.str();
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
        std::ostringstream details;
        details << buildCrashContextSummary() << '\n';
        details << "Exception: " << describeCurrentException() << '\n';
        details << "Backtrace:\n"
                << captureCurrentStackTrace(1);

        const std::filesystem::path crashFilePath = writeCrashReportInternal("Unhandled terminate", details.str(), true);
        if (!crashFilePath.empty())
            emitCrashMessage("Crash report written to: " + crashFilePath.string());

        std::_Exit(EXIT_FAILURE);
    }

#if defined(__linux__) || defined(__APPLE__)
    void signalCrashHandler(int signalValue, siginfo_t *signalInfo, void *)
    {
        std::ostringstream details;
        details << buildCrashContextSummary() << '\n';
        details << "Signal: " << signalNameFromValue(signalValue) << " (" << signalValue << ')' << '\n';
        if (signalInfo)
        {
            details << "Signal code: " << signalInfo->si_code << '\n';
            details << "Fault address: " << pointerToString(signalInfo->si_addr) << '\n';
        }
        details << "Backtrace:\n"
                << captureCurrentStackTrace(1);

        const std::filesystem::path crashFilePath = writeCrashReportInternal("Fatal signal", details.str(), true);
        if (!crashFilePath.empty())
            emitCrashMessage("Crash report written to: " + crashFilePath.string());

        std::_Exit(128 + signalValue);
    }
#else
    void signalCrashHandler(int signalValue)
    {
        std::ostringstream details;
        details << buildCrashContextSummary() << '\n';
        details << "Signal: " << signalNameFromValue(signalValue) << " (" << signalValue << ')' << '\n';
        details << "Backtrace:\n"
                << captureCurrentStackTrace(1);

        const std::filesystem::path crashFilePath = writeCrashReportInternal("Fatal signal", details.str(), true);
        if (!crashFilePath.empty())
            emitCrashMessage("Crash report written to: " + crashFilePath.string());

        std::_Exit(128 + signalValue);
    }
#endif

#if defined(_WIN32)
    LONG WINAPI windowsUnhandledExceptionHandler(EXCEPTION_POINTERS *exceptionPointers)
    {
        std::ostringstream details;
        details << buildCrashContextSummary() << '\n';
        if (exceptionPointers && exceptionPointers->ExceptionRecord)
        {
            details << "Exception code: 0x" << std::hex << std::uppercase << exceptionPointers->ExceptionRecord->ExceptionCode << std::dec << '\n';
            details << "Exception address: " << exceptionPointers->ExceptionRecord->ExceptionAddress << '\n';
        }
        else
        {
            details << "No structured exception information available.\n";
        }
        details << "Backtrace:\n"
                << captureCurrentStackTrace(1);

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
#if defined(__linux__) || defined(__APPLE__)
        struct sigaction signalAction{};
        signalAction.sa_flags = SA_SIGINFO | SA_RESETHAND;
        signalAction.sa_sigaction = signalCrashHandler;
        sigemptyset(&signalAction.sa_mask);

        sigaction(SIGABRT, &signalAction, nullptr);
        sigaction(SIGFPE, &signalAction, nullptr);
        sigaction(SIGILL, &signalAction, nullptr);
        sigaction(SIGSEGV, &signalAction, nullptr);
#ifdef SIGBUS
        sigaction(SIGBUS, &signalAction, nullptr);
#endif
#else
        std::signal(SIGABRT, signalCrashHandler);
        std::signal(SIGFPE, signalCrashHandler);
        std::signal(SIGILL, signalCrashHandler);
        std::signal(SIGSEGV, signalCrashHandler);
#ifdef SIGBUS
        std::signal(SIGBUS, signalCrashHandler);
#endif
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
