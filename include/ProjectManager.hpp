#ifndef PROJECT_MANAGER_HPP
#define PROJECT_MANAGER_HPP

#include "Project.hpp"

class ProjectManager
{
public:
    ProjectManager() = default;
    ~ProjectManager() = default;

    static ProjectManager& instance();

    Project* createProject(const std::string& projectName, const std::string& projectPath);

    bool loadConfigInProject(const std::string& configPath, Project* project);

    bool loadProject(Project* project);

    void setCurrentProject(Project* project);

    Project* getCurrentProject();

private:

    Project* m_currentProject{nullptr};


    ProjectManager(const ProjectManager&) = delete;
    ProjectManager& operator=(const ProjectManager&) = delete;
    ProjectManager(ProjectManager&&) = delete;
    ProjectManager& operator=(ProjectManager&&) = delete;
};

#endif //PROJECT_MANAGER_HPP
