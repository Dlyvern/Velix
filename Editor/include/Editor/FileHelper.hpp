#ifndef ELIX_FILE_HELPER_EDITOR_HPP
#define ELIX_FILE_HELPER_EDITOR_HPP

#include "Core/Macros.hpp"
#include "Core/Logger.hpp"

#include <string>
#include <filesystem>
#include <utility>

#if defined(_WIN32)
#define SHARED_LIB_EXTENSION ".dll"
#elif defined(__linux__)
#define SHARED_LIB_EXTENSION ".so"
#else
#define SHARED_LIB_EXTENSION ""
#endif

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class FileHelper
{
public:
    static std::pair<int, std::string> executeCommand(const std::string &command);
    static bool launchDetachedCommand(const std::string &command);

    static std::filesystem::path getExecutablePath();

    // static std::string getHomeDirectory()
    // {
    //     std::string homeDir;

    //     #ifdef _WIN32
    //         char* envValue = nullptr;
    //         size_t len = 0;
    //         if (_dupenv_s(&envValue, &len, "USERPROFILE") == 0 && envValue != nullptr)
    //         {
    //             homeDir = envValue;
    //             free(envValue);
    //         }
    //     #else
    //         if (const char* homeEnv = getenv("HOME"))
    //             homeDir = homeEnv;
    //         else
    //         {
    //             struct passwd* pw = getpwuid(getuid());

    //             if (pw)
    //                 homeDir = pw->pw_dir;
    //         }
    //     #endif

    //     if (!homeDir.empty() && homeDir.back() != '/' && homeDir.back() != '\\')
    //     {
    //         #ifdef _WIN32
    //             homeDir += '\\';
    //         #else
    //             homeDir += '/';
    //         #endif
    //     }

    //     return homeDir;
    // }

    // static std::string getDocumentsDirectory()
    // {
    //     #if defined(_WIN32) || defined(_WIN64)
    //         // Windows: Use %USERPROFILE%\Documents
    //         const char* userProfile = std::getenv("USERPROFILE");
    //         if (userProfile)
    //             return std::string(userProfile) + "\\Documents";
    //         else
    //             return {};

    //     #elif defined(__linux__)
    //         // Linux: Use XDG_DOCUMENTS_DIR or ~/Documents
    //         const char* xdgDocs = std::getenv("XDG_DOCUMENTS_DIR");
    //         if (xdgDocs)
    //             return std::string(xdgDocs);

    //         std::string homeDirectory = getHomeDirectory();

    //         if (!homeDirectory.empty())
    //             return std::string(homeDirectory) + "Documents";

    //         return {};
    //     #else
    //         return {};
    //     #endif
    // }
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_FILE_HELPER_EDITOR_HPP
