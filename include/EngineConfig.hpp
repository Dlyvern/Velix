#ifndef ENGINE_CONFIG_HPP
#define ENGINE_CONFIG_HPP

#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
#else
    #include <unistd.h>
    #include <pwd.h>
#endif

#include "../libraries/json/json.hpp"

class EngineConfig
{
public:
    bool load(const std::string& path = "");

    nlohmann::json getConfig();

private:
    std::string getConfigPath();

    nlohmann::json m_config;
};

#endif //ENGINE_CONFIG_HPP