#ifndef ELIX_ENGINE_DIAGNOSTICS_HPP
#define ELIX_ENGINE_DIAGNOSTICS_HPP

#include "Core/Macros.hpp"

#include <filesystem>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace diagnostics
{
    std::filesystem::path getExecutablePath();
    std::filesystem::path getExecutableDirectory();
    std::filesystem::path ensureLogsDirectory();
    std::filesystem::path configureDefaultLogging(const std::string &baseFileName = "velix");
    std::filesystem::path getActiveLogFilePath();
    void installCrashHandler(const std::filesystem::path &logsDirectory = {});
    std::filesystem::path writeCrashReport(const std::string &reason, const std::string &details = {});
}

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ENGINE_DIAGNOSTICS_HPP
