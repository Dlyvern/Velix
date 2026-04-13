#include "Editor/FileHelper.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <shlobj.h>
#elif defined(__linux__)
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#endif

#include <cstdint>
#include <cstdlib>
#include <array>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

std::pair<int, std::string> FileHelper::executeCommand(const std::string &command)
{
    constexpr int kBufferSize = 128;
    std::array<char, kBufferSize> buffer{};
    std::string result;
    int exitCode = -1;

#ifdef _WIN32
    // Build the pipe command, redirecting stderr to stdout.
    std::string pipeCmd = command + " 2>&1";

    // cmd.exe (invoked by _popen) requires the ENTIRE command to be wrapped in an
    // extra pair of outer double-quotes when the executable token itself is quoted.
    // Without this, cmd.exe strips the first quoted token as the outer delimiter
    // and misinterprets the rest, producing ERROR_INVALID_NAME.
    if (!pipeCmd.empty() && pipeCmd[0] == '"')
        pipeCmd = "\"" + pipeCmd + "\"";

    // Convert the UTF-8 command string to UTF-16 so that _wpopen can handle paths
    // containing characters outside the current ANSI code page (e.g. accented letters,
    // Cyrillic, CJK).  _popen uses the narrow ANSI code page and silently garbles or
    // rejects such paths.
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, pipeCmd.c_str(), -1, nullptr, 0);
    std::wstring wideCmd(wideLen > 0 ? static_cast<size_t>(wideLen - 1) : 0u, L'\0');
    if (wideLen > 0)
        MultiByteToWideChar(CP_UTF8, 0, pipeCmd.c_str(), -1, wideCmd.data(), wideLen);

    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_wpopen(wideCmd.c_str(), L"r"), _pclose);
#else
    std::string commandWithStderr = command + " 2>&1";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(commandWithStderr.c_str(), "r"), pclose);
#endif

    if (!pipe)
    {
        VX_EDITOR_ERROR_STREAM("Failed to execute command: " << command);
        return {-1, ""};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr)
        result += buffer.data();

#ifdef _WIN32
    exitCode = _pclose(pipe.release());
#else
    const int statusCode = pclose(pipe.release());
    if (WIFEXITED(statusCode))
        exitCode = WEXITSTATUS(statusCode);
    else
        exitCode = statusCode;
#endif

    return {exitCode, result};
}

bool FileHelper::launchDetachedCommand(const std::string &command)
{
    if (command.empty())
    {
        VX_EDITOR_ERROR_STREAM("Failed to launch command: empty command\n");
        return false;
    }

#ifdef _WIN32
    STARTUPINFOA startupInfo{};
    PROCESS_INFORMATION processInfo{};
    startupInfo.cb = sizeof(startupInfo);

    std::string launchCommand = "cmd /C start \"\" " + command;
    std::vector<char> mutableCommand(launchCommand.begin(), launchCommand.end());
    mutableCommand.push_back('\0');

    const BOOL created = CreateProcessA(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    if (!created)
    {
        VX_EDITOR_ERROR_STREAM("Failed to launch detached command: " << command);
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
#else
    const pid_t processId = fork();
    if (processId < 0)
    {
        VX_EDITOR_ERROR_STREAM("Failed to fork for detached command: " << command);
        return false;
    }

    if (processId == 0)
    {
        setsid();
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }

    return true;
#endif
}

std::filesystem::path FileHelper::getExecutablePath()
{
    const std::filesystem::path executableFilePath = getExecutableFilePath();
    if (executableFilePath.empty())
        return {};

    return executableFilePath.parent_path();
}

std::filesystem::path FileHelper::getExecutableFilePath()
{
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0)
        return {};

    while (size >= buffer.size() - 1u)
    {
        buffer.resize(buffer.size() * 2u, L'\0');
        size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0)
            return {};
    }

    buffer.resize(size);
    return std::filesystem::path(buffer);

#elif defined(__linux__)
    char buffer[1024];
    ssize_t size = readlink("/proc/self/exe", buffer, sizeof(buffer));
    if (size <= 0 || size >= static_cast<ssize_t>(sizeof(buffer)))
        return {};
    return std::filesystem::path(std::string(buffer, size));
#else
    return {};
#endif
}

ELIX_NESTED_NAMESPACE_END
