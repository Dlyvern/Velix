#include "Engine/Runtime/EngineConfig.hpp"

#include "Core/Logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <unordered_set>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace
{
    struct IdeDefinition
    {
        std::string id;
        std::string displayName;
        std::vector<std::string> commands;
    };

    const std::vector<IdeDefinition> &getIdeDefinitions()
    {
        static const std::vector<IdeDefinition> definitions = {
            {"vscode", "Visual Studio Code", {"code"}},
            {"vscode_insiders", "Visual Studio Code Insiders", {"code-insiders"}},
            {"vscodium", "VSCodium", {"codium"}},
            {"clion", "CLion", {"clion", "clion.sh"}},
            {"rider", "Rider", {"rider", "rider.sh"}}};

        return definitions;
    }

    bool isVSCodeIdeId(const std::string &ideId)
    {
        return ideId == "vscode" || ideId == "vscode_insiders" || ideId == "vscodium";
    }

    std::vector<std::filesystem::path> splitPathEnvironment(const std::string &pathEnvironment)
    {
        std::vector<std::filesystem::path> paths;
        const char delimiter =
#if defined(_WIN32)
            ';';
#else
            ':';
#endif

        size_t startIndex = 0;
        while (startIndex <= pathEnvironment.size())
        {
            const size_t endIndex = pathEnvironment.find(delimiter, startIndex);
            const size_t segmentSize = (endIndex == std::string::npos)
                                           ? (pathEnvironment.size() - startIndex)
                                           : (endIndex - startIndex);

            if (segmentSize > 0)
                paths.emplace_back(pathEnvironment.substr(startIndex, segmentSize));

            if (endIndex == std::string::npos)
                break;

            startIndex = endIndex + 1;
        }

        return paths;
    }

    bool isExecutableFile(const std::filesystem::path &path)
    {
        if (path.empty() || !std::filesystem::exists(path) || !std::filesystem::is_regular_file(path))
            return false;

#if defined(_WIN32)
        return _access(path.string().c_str(), 0) == 0;
#else
        return access(path.c_str(), X_OK) == 0;
#endif
    }

    std::optional<std::filesystem::path> findExecutableInPath(const std::string &command)
    {
        if (command.empty())
            return std::nullopt;

        const std::filesystem::path commandPath(command);
        if (commandPath.is_absolute() && isExecutableFile(commandPath))
            return commandPath;

        const char *pathEnvironment = std::getenv("PATH");
        if (!pathEnvironment || std::string(pathEnvironment).empty())
            return std::nullopt;

        const auto pathEntries = splitPathEnvironment(pathEnvironment);
        for (const auto &entry : pathEntries)
        {
            if (entry.empty() || !std::filesystem::exists(entry))
                continue;

            std::filesystem::path candidate = entry / command;
            if (isExecutableFile(candidate))
                return candidate;

#if defined(_WIN32)
            if (!candidate.has_extension())
            {
                static const std::array<std::string, 4> windowsExtensions = {".exe", ".cmd", ".bat", ".com"};
                for (const auto &extension : windowsExtensions)
                {
                    const std::filesystem::path candidateWithExtension = candidate;
                    const std::filesystem::path fullPath = candidateWithExtension.string() + extension;
                    if (isExecutableFile(fullPath))
                        return fullPath;
                }
            }
#endif
        }

        return std::nullopt;
    }
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)

EngineConfig &EngineConfig::instance()
{
    static EngineConfig config;
    return config;
}

bool EngineConfig::reload()
{
    applyDefaults();

    bool loadOk = loadFromDisk();
    detectInstalledIdes();

    if (!isKnownIdeId(m_preferredIdeId))
        m_preferredIdeId = "vscode";

    if (!findIde(m_preferredIdeId).has_value())
    {
        if (auto preferredVSCode = findPreferredVSCodeIde())
            m_preferredIdeId = preferredVSCode->id;
        else if (!m_detectedIdes.empty())
            m_preferredIdeId = m_detectedIdes.front().id;
    }

    if (!std::filesystem::exists(m_configFilePath))
    {
        if (!save())
            return false;
    }

    return loadOk;
}

bool EngineConfig::save() const
{
    try
    {
        if (!m_configDirectory.empty())
            std::filesystem::create_directories(m_configDirectory);
    }
    catch (const std::exception &exception)
    {
        VX_ENGINE_ERROR_STREAM("Failed to create config directory '" << m_configDirectory << "': " << exception.what() << '\n');
        return false;
    }

    nlohmann::json json;
    json["version"] = 1;
    json["preferred_ide"] = m_preferredIdeId;

    std::ofstream file(m_configFilePath);
    if (!file.is_open())
    {
        VX_ENGINE_ERROR_STREAM("Failed to save engine config to '" << m_configFilePath << "'\n");
        return false;
    }

    file << std::setw(4) << json << '\n';
    return file.good();
}

const std::filesystem::path &EngineConfig::getConfigDirectory() const
{
    return m_configDirectory;
}

const std::filesystem::path &EngineConfig::getConfigFilePath() const
{
    return m_configFilePath;
}

const std::vector<EngineConfig::IdeInfo> &EngineConfig::getDetectedIdes() const
{
    return m_detectedIdes;
}

std::optional<EngineConfig::IdeInfo> EngineConfig::findIde(const std::string &ideId) const
{
    auto ideIterator = std::find_if(m_detectedIdes.begin(), m_detectedIdes.end(), [&](const IdeInfo &ide)
                                    { return ide.id == ideId; });

    if (ideIterator == m_detectedIdes.end())
        return std::nullopt;

    return *ideIterator;
}

const std::string &EngineConfig::getPreferredIdeId() const
{
    return m_preferredIdeId;
}

void EngineConfig::setPreferredIdeId(const std::string &ideId)
{
    if (!isKnownIdeId(ideId))
        return;

    m_preferredIdeId = ideId;
}

std::optional<EngineConfig::IdeInfo> EngineConfig::findPreferredVSCodeIde() const
{
    if (isVSCodeIdeId(m_preferredIdeId))
    {
        if (auto ide = findIde(m_preferredIdeId))
            return ide;
    }

    static const std::array<std::string, 3> vscodeIdePriority = {
        "vscode",
        "vscode_insiders",
        "vscodium"};

    for (const auto &ideId : vscodeIdePriority)
    {
        if (auto ide = findIde(ideId))
            return ide;
    }

    return std::nullopt;
}

bool EngineConfig::hasVSCodeIde() const
{
    return findPreferredVSCodeIde().has_value();
}

std::filesystem::path EngineConfig::resolveConfigDirectory()
{
#if defined(_WIN32)
    if (const char *appData = std::getenv("APPDATA"))
        if (*appData != '\0')
            return std::filesystem::path(appData) / "Velix";

    if (const char *localAppData = std::getenv("LOCALAPPDATA"))
        if (*localAppData != '\0')
            return std::filesystem::path(localAppData) / "Velix";

    if (const char *userProfile = std::getenv("USERPROFILE"))
        if (*userProfile != '\0')
            return std::filesystem::path(userProfile) / "AppData" / "Roaming" / "Velix";

    return std::filesystem::current_path() / ".velix";
#else
    if (const char *xdgConfigHome = std::getenv("XDG_CONFIG_HOME"))
        if (*xdgConfigHome != '\0')
            return std::filesystem::path(xdgConfigHome) / "Velix";

    if (const char *home = std::getenv("HOME"))
        if (*home != '\0')
            return std::filesystem::path(home) / ".config" / "Velix";

    return std::filesystem::current_path() / ".config" / "Velix";
#endif
}

void EngineConfig::detectInstalledIdes()
{
    m_detectedIdes.clear();

    std::unordered_set<std::string> addedIdeIds;
    for (const auto &ideDefinition : getIdeDefinitions())
    {
        for (const auto &command : ideDefinition.commands)
        {
            auto executablePath = findExecutableInPath(command);
            if (!executablePath.has_value())
                continue;

            if (!addedIdeIds.insert(ideDefinition.id).second)
                break;

            IdeInfo ideInfo;
            ideInfo.id = ideDefinition.id;
            ideInfo.displayName = ideDefinition.displayName;
            ideInfo.command = executablePath->string();
            m_detectedIdes.emplace_back(std::move(ideInfo));
            break;
        }
    }
}

void EngineConfig::applyDefaults()
{
    m_configDirectory = resolveConfigDirectory();
    m_configFilePath = m_configDirectory / "engine_config.json";
    m_preferredIdeId = "vscode";
}

bool EngineConfig::loadFromDisk()
{
    if (m_configFilePath.empty() || !std::filesystem::exists(m_configFilePath))
        return true;

    std::ifstream file(m_configFilePath);
    if (!file.is_open())
    {
        VX_ENGINE_WARNING_STREAM("Failed to open engine config file: " << m_configFilePath << '\n');
        return false;
    }

    nlohmann::json json;

    try
    {
        file >> json;
    }
    catch (const nlohmann::json::parse_error &exception)
    {
        VX_ENGINE_WARNING_STREAM("Failed to parse engine config file '" << m_configFilePath << "': " << exception.what() << '\n');
        return false;
    }

    if (json.contains("preferred_ide") && json["preferred_ide"].is_string())
        m_preferredIdeId = json["preferred_ide"].get<std::string>();

    return true;
}

bool EngineConfig::isKnownIdeId(const std::string &ideId) const
{
    const auto &definitions = getIdeDefinitions();
    return std::any_of(definitions.begin(), definitions.end(), [&](const IdeDefinition &definition)
                       { return definition.id == ideId; });
}

ELIX_NESTED_NAMESPACE_END
