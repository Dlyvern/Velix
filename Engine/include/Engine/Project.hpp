#ifndef PROJECT_HPP
#define PROJECT_HPP

#include <string>

struct Project
{
public:
    void* projectLibrary;
    std::string assetsDir;
    std::string entryScene;
    std::string scenesDir;
    std::string name;
    std::string fullPath;
    std::string buildDir;
    std::string sourceDir;
    std::string exportDir;
};

#endif //PROJECT_HPP
