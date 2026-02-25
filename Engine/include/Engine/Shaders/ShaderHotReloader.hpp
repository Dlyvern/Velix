#ifndef ELIX_SHADER_HOT_RELOADER_HPP
#define ELIX_SHADER_HOT_RELOADER_HPP

#include "Core/Macros.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(shaders)

class ShaderHotReloader
{
public:
    explicit ShaderHotReloader(std::filesystem::path shadersRootPath = "./resources/shaders");

    void setPollIntervalSeconds(double seconds);
    void setShadersRootPath(const std::filesystem::path &path);

    void prime();

    bool update(double deltaTimeSeconds);
    bool consumeReloadRequest();

private:
    bool shouldTrackSourceFile(const std::filesystem::path &path) const;
    bool scanAndCompileChangedFiles();

    std::filesystem::path m_shadersRootPath;
    double m_pollIntervalSeconds{0.5};
    double m_accumulatedDeltaSeconds{0.0};
    bool m_reloadRequested{false};

    std::unordered_map<std::string, std::filesystem::file_time_type> m_fileWriteTimes;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SHADER_HOT_RELOADER_HPP
