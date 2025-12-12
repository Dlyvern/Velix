#include "Editor/FileHelper.hpp"

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <cstdio>
    #include <shlobj.h>
#elif defined(__linux__)
    #include <unistd.h>
    #include <sys/types.h>
    #include <pwd.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
    #include <sys/types.h>
    #include <unistd.h>
    #include <pwd.h>
#endif

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <array>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

std::pair<int, std::string> FileHelper::executeCommand(const std::string& command)
{
    constexpr int kBufferSize = 128;
    std::array<char, kBufferSize> buffer{};
    std::string result;

#ifdef _WIN32
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
#else
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
#endif

    if (!pipe) 
    {
        std::cerr << "Failed to execute command";
        return {-1, ""};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr)
        result += buffer.data();

    int executionResult = std::system(command.c_str());

    return {executionResult, result};
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

#elif defined(__APPLE__)
    char buffer[1024];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0)
        return {};
    return std::filesystem::path(buffer).parent_path();
#else
    return {};
#endif
}


ELIX_NESTED_NAMESPACE_END