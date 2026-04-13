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
    // Use CreateProcess + an anonymous pipe to capture stdout+stderr.
    // This bypasses cmd.exe entirely, so there are no cmd.exe quoting quirks
    // and Unicode paths (UTF-8 → UTF-16) are handled correctly.

    // Convert UTF-8 command string to UTF-16.
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, nullptr, 0);
    std::wstring wideCmd(wideLen > 0 ? static_cast<size_t>(wideLen - 1) : 0u, L'\0');
    if (wideLen > 0)
        MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, wideCmd.data(), wideLen);

    // Create an anonymous pipe; the child inherits the write end for stdout+stderr.
    SECURITY_ATTRIBUTES sa{};
    sa.nLength              = sizeof(sa);
    sa.bInheritHandle       = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
    {
        VX_EDITOR_ERROR_STREAM("Failed to create pipe for command: " << command);
        return {-1, ""};
    }
    // Prevent the read end from being inherited by the child.
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = hWritePipe;
    si.hStdError  = hWritePipe;
    si.hStdInput  = nullptr;

    PROCESS_INFORMATION pi{};
    const BOOL created = CreateProcessW(
        nullptr,           // derive executable from command line
        wideCmd.data(),    // mutable UTF-16 command line
        nullptr, nullptr,  // process / thread security attributes
        TRUE,              // inherit handles (write pipe)
        CREATE_NO_WINDOW,  // no console window
        nullptr, nullptr,  // environment / working directory (inherit)
        &si, &pi);

    // Close the write end in the parent now that the child has inherited it.
    // ReadFile will return EOF when the child exits and closes its own copy.
    CloseHandle(hWritePipe);

    if (!created)
    {
        CloseHandle(hReadPipe);
        VX_EDITOR_ERROR_STREAM("Failed to execute command: " << command);
        return {-1, ""};
    }

    // Drain all output.
    DWORD bytesRead = 0;
    while (ReadFile(hReadPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0)
        result.append(buffer.data(), bytesRead);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD winExitCode = 0;
    GetExitCodeProcess(pi.hProcess, &winExitCode);
    exitCode = static_cast<int>(winExitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);

    return {exitCode, result};
#else
    std::string commandWithStderr = command + " 2>&1";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(commandWithStderr.c_str(), "r"), pclose);

    if (!pipe)
    {
        VX_EDITOR_ERROR_STREAM("Failed to execute command: " << command);
        return {-1, ""};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr)
        result += buffer.data();

    const int statusCode = pclose(pipe.release());
    if (WIFEXITED(statusCode))
        exitCode = WEXITSTATUS(statusCode);
    else
        exitCode = statusCode;

    return {exitCode, result};
#endif
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
