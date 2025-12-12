#ifndef ELIX_FILE_HELPER_EDITOR_HPP
#define ELIX_FILE_HELPER_EDITOR_HPP 

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <cstdio>
    #undef near
    #undef far
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

#include "Core/Macros.hpp"

#include <string>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <array>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

#if defined(_WIN32)
    #define SHARED_LIB_EXTENSION ".dll"
#elif defined(__linux__)
    #define SHARED_LIB_EXTENSION ".so"
#elif defined(__APPLE__)
    #define SHARED_LIB_EXTENSION ".dylib"
#else
    #define SHARED_LIB_EXTENSION ""
#endif


// static constexpr const char* WINDOW_SHARED_LIB_EXTENSION = ".dll";
// static constexpr const char* MACOS_SHARED_LIB_EXTENSION = ".dylib";
// static constexpr const char* LINUX_SHARED_LIB_EXTENSION = ".so";

class FileHelper
{
public:
    static std::pair<int, std::string> executeCommand(const std::string& command)
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

    static std::string getHomeDirectory()
    {
        std::string homeDir;

        #ifdef _WIN32
            char* envValue = nullptr;
            size_t len = 0;
            if (_dupenv_s(&envValue, &len, "USERPROFILE") == 0 && envValue != nullptr)
            {
                homeDir = envValue;
                free(envValue);
            }
        #else
            if (const char* homeEnv = getenv("HOME"))
                homeDir = homeEnv; 
            else 
            {
                struct passwd* pw = getpwuid(getuid());

                if (pw)
                    homeDir = pw->pw_dir;
            }
        #endif

        if (!homeDir.empty() && homeDir.back() != '/' && homeDir.back() != '\\') 
        {
            #ifdef _WIN32
                homeDir += '\\';
            #else
                homeDir += '/';
            #endif
        }

        return homeDir;
    }

    static std::string getDocumentsDirectory()
    {
        #if defined(_WIN32) || defined(_WIN64)
            // Windows: Use %USERPROFILE%\Documents
            const char* userProfile = std::getenv("USERPROFILE");
            if (userProfile)
                return std::string(userProfile) + "\\Documents";
            else
                return {};

        #elif defined(__APPLE__)
            // macOS: Use HOME/Documents
            const char* home = std::getenv("HOME");
            if (home)
                return std::string(home) + "/Documents";
            else
                return {};

        #elif defined(__linux__)
            // Linux: Use XDG_DOCUMENTS_DIR or ~/Documents
            const char* xdgDocs = std::getenv("XDG_DOCUMENTS_DIR");
            if (xdgDocs)
                return std::string(xdgDocs);

            std::string homeDirectory = getHomeDirectory();

            if (!homeDirectory.empty())
                return std::string(homeDirectory) + "Documents";

            return {};
        #else
            return {};
        #endif
    }

    static std::filesystem::path getExecutablePath()
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
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_FILE_HELPER_EDITOR_HPP 