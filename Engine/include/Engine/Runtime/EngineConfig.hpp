#ifndef ELIX_ENGINE_CONFIG_HPP
#define ELIX_ENGINE_CONFIG_HPP

#include "Core/Macros.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class EngineConfig
{
public:
    struct IdeInfo
    {
        std::string id;
        std::string displayName;
        std::string command;
    };

    static EngineConfig &instance();

    bool reload();
    bool save() const;

    const std::filesystem::path &getConfigDirectory() const;
    const std::filesystem::path &getConfigFilePath() const;

    const std::vector<IdeInfo> &getDetectedIdes() const;
    std::optional<IdeInfo> findIde(const std::string &ideId) const;

    const std::string &getPreferredIdeId() const;
    void setPreferredIdeId(const std::string &ideId);

    std::optional<IdeInfo> findPreferredVSCodeIde() const;
    bool hasVSCodeIde() const;

private:
    EngineConfig() = default;

    static std::filesystem::path resolveConfigDirectory();
    void detectInstalledIdes();
    void applyDefaults();
    bool loadFromDisk();
    bool isKnownIdeId(const std::string &ideId) const;

private:
    std::filesystem::path m_configDirectory;
    std::filesystem::path m_configFilePath;
    std::string m_preferredIdeId{"vscode"};
    std::vector<IdeInfo> m_detectedIdes;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ENGINE_CONFIG_HPP
