#ifndef ELIX_PROJECT_LOADER_HPP
#define ELIX_PROJECT_LOADER_HPP

#include "Core/Macros.hpp"
#include "Editor/Project.hpp"

#include <memory>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class ProjectLoader
{
public:
    static std::shared_ptr<Project> loadProject(const std::string &projectPath);

    ProjectLoader() = delete;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PROJECT_LOADER_HPP