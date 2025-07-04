#ifndef PROJECT_HPP
#define PROJECT_HPP


#include <string>

class Project
{
public:

    void setFullPath(const std::string& fullPath);
    void setName(const std::string& name);
    void setBuildDir(const std::string& buildDir);
    void setSourceDir(const std::string& sourceDir);
    void setScenesDir(const std::string& scenesDir);
    void setEntryScene(const std::string& entryScene);
    void setAssetsDir(const std::string& assetsDir);
    void setProjectLibrary(void* projectLibrary);

    [[nodiscard]] void* getProjectLibrary() const;
    [[nodiscard]] std::string getAssetsDir() const;
    [[nodiscard]] std::string getEntryScene() const;
    [[nodiscard]]std::string getScenesDir() const;
    [[nodiscard]]std::string getSourceDir() const;
    [[nodiscard]]std::string getBuildDir() const;
    [[nodiscard]]std::string getName() const;
    [[nodiscard]]std::string getFullPath() const;


private:
    void* m_projectLibrary;
    std::string m_assetsDir;
    std::string m_entryScene;
    std::string m_scenesDir;
    std::string m_name;
    std::string m_fullPath;
    std::string m_buildDir;
    std::string m_sourceDir;
};

#endif //PROJECT_HPP
