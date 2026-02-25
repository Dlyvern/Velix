#include "Editor/ProjectLoader.hpp"

#include <filesystem>
#include <iostream>
#include <fstream>

#include "Engine/Assets/AssetsLoader.hpp"

#include "nlohmann/json.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

std::shared_ptr<Project> ProjectLoader::loadProject(const std::string &projectPath)
{
    if (projectPath.empty())
    {
        VX_EDITOR_ERROR_STREAM("Project path is empty\n");
        return nullptr;
    }

    if (!std::filesystem::exists(projectPath))
    {
        VX_EDITOR_ERROR_STREAM("Project path does not exist\n");
        return nullptr;
    }

    std::string projectConfigPath;

    const std::string defaultProjectConfigFile = projectPath.back() == '/' ? projectPath + "/project.elixproject" : projectPath + "project.elixproject";

    if (!std::filesystem::exists(defaultProjectConfigFile))
    {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(projectPath))
        {
            if (entry.path().extension() == ".elixproject")
            {
                projectConfigPath = entry.path().string();
                break;
            }
        }

        VX_EDITOR_ERROR_STREAM("Failed to find config file in project\n");

        return nullptr;
    }
    else
        projectConfigPath = defaultProjectConfigFile;

    std::ifstream configFile(projectConfigPath);

    if (!configFile.is_open())
    {
        VX_EDITOR_ERROR_STREAM("Failed to open config file: " << projectConfigPath << '\n');
        return nullptr;
    }

    nlohmann::json json;

    try
    {
        configFile >> json;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        VX_EDITOR_ERROR_STREAM("Failed to parse scene file " << e.what() << '\n');
        return nullptr;
    }

    // void* projectLibrary;
    // std::string assetsDir;
    // std::string entryScene;
    // std::string scenesDir;
    // std::string name;
    // std::string fullPath;
    // std::string buildDir;
    // std::string sourceDir;
    // std::string exportDir;

    if (!json.contains("path")) //*Very important
    {
        VX_EDITOR_ERROR_STREAM("Failed to find 'path' key in config. Aborting...\n");
        return nullptr;
    }

    if (!json.contains("scene"))
    {
        VX_EDITOR_INFO_STREAM("No scene. Creating default scene\n");
    }

    auto project = std::make_shared<Project>();

    project->name = json["name"];
    project->entryScene = json["scene"];
    project->fullPath = json["path"];
    project->resourcesDir = json["resources_path"];
    project->sourcesDir = json["sources_path"];

    auto &assetsCache = project->cache;

    if (!std::filesystem::exists(project->fullPath))
    {
        VX_EDITOR_ERROR_STREAM("Project full path is not correct\n");
        return nullptr;
    }

    for (auto &entry : std::filesystem::recursive_directory_iterator(project->fullPath))
    {
        const auto &extension = entry.path().extension();

        if (extension == ".png" || extension == ".jpg")
        {
            VX_EDITOR_INFO_STREAM("Found texture: " << entry.path().string());

            TextureAssetRecord texture;
            texture.path = entry.path().string();

            assetsCache.texturesByPath[texture.path] = texture;
        }
    }

    return project;
}

ELIX_NESTED_NAMESPACE_END