#ifndef PROJECT_HPP
#define PROJECT_HPP

#include <string>

#include "Core/Macros.hpp"
#include "Engine/PluginSystem/PluginLoader.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct Project
{
public:
    LibraryHandle projectLibrary;
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
