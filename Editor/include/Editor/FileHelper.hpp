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

    static std::filesystem::path getExecutableFilePath();
    static std::filesystem::path getExecutablePath();































































};

ELIX_NESTED_NAMESPACE_END

#endif
