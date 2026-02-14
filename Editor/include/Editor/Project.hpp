#ifndef PROJECT_HPP
#define PROJECT_HPP

#include <string>
#include <vector>

#include "Core/Macros.hpp"
#include "Engine/PluginSystem/PluginLoader.hpp"
#include "Engine/Assets/AssetsCache.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

struct ProjectCache
{
    engine::AssetsCache assetsCache;

    std::vector<std::string> allProjectTexturesPaths; // TODO FIX THIS SHIT(we use to avoid texture(GPU) loading on project loading)
};

struct Project
{
public:
    ProjectCache cache;
    engine::LibraryHandle projectLibrary;
    std::string resourcesDir;
    std::string entryScene;
    std::string scenesDir;
    std::string name;
    std::string fullPath;
    std::string buildDir;
    std::string sourcesDir;
    std::string exportDir;
};

ELIX_NESTED_NAMESPACE_END

#endif // PROJECT_HPP
