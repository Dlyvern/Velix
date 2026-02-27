#include "Editor/FileHelper.hpp"

#if defined(_WIN32)
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

    std::string commandWithStderr = command + " 2>&1";

#ifdef _WIN32
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(commandWithStderr.c_str(), "r"), _pclose);
#else
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
#if defined(_WIN32)
    char buffer[MAX_PATH];
    DWORD size = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (size == 0 || size == MAX_PATH)
        return {};
    return std::filesystem::path(buffer).parent_path();

#elif defined(__linux__)
    char buffer[1024];
    ssize_t size = readlink("/proc/self/exe", buffer, sizeof(buffer));
    if (size <= 0 || size >= static_cast<ssize_t>(sizeof(buffer)))
        return {};
    return std::filesystem::path(std::string(buffer, size)).parent_path();
#else
    return {};
#endif
}

ELIX_NESTED_NAMESPACE_END
