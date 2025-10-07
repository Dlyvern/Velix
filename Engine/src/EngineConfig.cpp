#include "Engine/EngineConfig.hpp"
#include <stdexcept>
bool EngineConfig::load(const std::string& path)
{
    const std::string configPath = path.empty() ? getConfigPath() : path;

    if(std::filesystem::exists(configPath))
    {
        std::ifstream ifs(configPath);

        if(!ifs)
            return false;
        
        ifs >> m_config;
    }
    else
    {
        m_config = 
        {
            {"graphicsAPI", "OpenGL"}
        };

        std::ofstream ofs(configPath);
        
        if(!ofs)
            return false;

        ofs << m_config.dump(4);
    }

    return true;
}

nlohmann::json EngineConfig::getConfig()
{
    return m_config;
}

std::string EngineConfig::getConfigPath()
{
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path)))
    {
        std::filesystem::path configPath = std::filesystem::path(path) / "VelixEngine";
        std::filesystem::create_directories(configPath);
        return (configPath / "config.json").string();
    }
    else
        throw std::runtime_error("Failed to get APPDATA folder");
#else
    const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");

    std::filesystem::path configDir;

    if (xdgConfigHome && strlen(xdgConfigHome) > 0)
        configDir = std::filesystem::path(xdgConfigHome) / "VelixEngine";
    else
    {
        const char* homeDir = std::getenv("HOME");
        if (!homeDir)
        {
            struct passwd* pwd = getpwuid(getuid());
            if (pwd)
                homeDir = pwd->pw_dir;
            else
                throw std::runtime_error("Cannot find home directory");
        }
        configDir = std::filesystem::path(homeDir) / ".config" / "VelixEngine";
    }

    std::filesystem::create_directories(configDir);

    return (configDir / "config.json").string();
#endif
}