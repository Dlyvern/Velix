#include "Engine/Shaders/ShaderHotReloader.hpp"

#include "Engine/Shaders/ShaderCompiler.hpp"

#include <algorithm>
#include <unordered_set>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(shaders)

ShaderHotReloader::ShaderHotReloader(std::filesystem::path shadersRootPath) : m_shadersRootPath(std::move(shadersRootPath))
{
}

void ShaderHotReloader::setPollIntervalSeconds(double seconds)
{
    m_pollIntervalSeconds = std::max(seconds, 0.0);
}

void ShaderHotReloader::setShadersRootPath(const std::filesystem::path &path)
{
    m_shadersRootPath = path;
    m_fileWriteTimes.clear();
    m_reloadRequested = false;
    m_accumulatedDeltaSeconds = 0.0;
}

void ShaderHotReloader::prime()
{
    m_fileWriteTimes.clear();

    if (!std::filesystem::exists(m_shadersRootPath) || !std::filesystem::is_directory(m_shadersRootPath))
        return;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(m_shadersRootPath))
    {
        if (!entry.is_regular_file())
            continue;

        const auto &path = entry.path();

        if (!shouldTrackSourceFile(path))
            continue;

        m_fileWriteTimes[path.string()] = std::filesystem::last_write_time(path);
    }
}

bool ShaderHotReloader::update(double deltaTimeSeconds)
{
    if (m_pollIntervalSeconds <= 0.0)
        return scanAndCompileChangedFiles();

    m_accumulatedDeltaSeconds += deltaTimeSeconds;

    if (m_accumulatedDeltaSeconds < m_pollIntervalSeconds)
        return false;

    m_accumulatedDeltaSeconds = 0.0;
    return scanAndCompileChangedFiles();
}

bool ShaderHotReloader::consumeReloadRequest()
{
    const bool reloadRequested = m_reloadRequested;
    m_reloadRequested = false;
    return reloadRequested;
}

bool ShaderHotReloader::shouldTrackSourceFile(const std::filesystem::path &path) const
{
    return ShaderCompiler::isCompilableShaderSource(path);
}

bool ShaderHotReloader::scanAndCompileChangedFiles()
{
    if (!std::filesystem::exists(m_shadersRootPath) || !std::filesystem::is_directory(m_shadersRootPath))
        return false;

    bool compiledAtLeastOneShader = false;
    std::unordered_set<std::string> seenFiles;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(m_shadersRootPath))
    {
        if (!entry.is_regular_file())
            continue;

        const auto &sourcePath = entry.path();

        if (!shouldTrackSourceFile(sourcePath))
            continue;

        const std::string sourcePathString = sourcePath.string();
        seenFiles.insert(sourcePathString);

        const auto writeTime = std::filesystem::last_write_time(sourcePath);
        auto writeTimeIt = m_fileWriteTimes.find(sourcePathString);

        const bool isNewSource = writeTimeIt == m_fileWriteTimes.end();
        const bool isModified = !isNewSource && writeTime > writeTimeIt->second;

        if (!isNewSource && !isModified)
            continue;

        std::string compileError;
        if (ShaderCompiler::compileFileToSpv(sourcePath, &compileError))
        {
            m_reloadRequested = true;
            compiledAtLeastOneShader = true;
            VX_ENGINE_INFO_STREAM("Hot reloaded shader source: " << sourcePathString);
        }
        else
            VX_ENGINE_ERROR_STREAM("Hot shader compile failed: " << compileError);

        m_fileWriteTimes[sourcePathString] = writeTime;
    }

    for (auto iterator = m_fileWriteTimes.begin(); iterator != m_fileWriteTimes.end();)
    {
        if (seenFiles.find(iterator->first) == seenFiles.end())
            iterator = m_fileWriteTimes.erase(iterator);
        else
            ++iterator;
    }

    return compiledAtLeastOneShader;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
