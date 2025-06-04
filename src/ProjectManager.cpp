#include "ProjectManager.hpp"
#include <filesystem>
#include <ElixirCore/SceneManager.hpp>

#include "ElixirCore/Logger.hpp"
#include <fstream>
#include <../libraries/json/json.hpp>
#include <ElixirCore/AssetsManager.hpp>

ProjectManager & ProjectManager::instance()
{
    static ProjectManager instance;

    return instance;
}

Project* ProjectManager::createProject(const std::string& projectName, const std::string& projectPath)
{
    if (std::filesystem::exists(projectPath))
    {
        LOG_WARN("Path already exists");
        return nullptr;
    }

    std::filesystem::create_directories(projectPath);

    std::string templatePath = "../ProjectTemplate";

    if (!std::filesystem::exists(templatePath))
    {
        LOG_ERROR("There is no template");
        return nullptr;
    }

    std::filesystem::copy(templatePath, projectPath, std::filesystem::copy_options::recursive);

    std::string projectFile = projectPath + "/Project.elixirproject";

    nlohmann::json projectJson;

    projectJson["name"] = projectName;
    projectJson["engine_version"] = "0.0.1";
    projectJson["project_path"] = projectPath;
    projectJson["entry_scene"] = projectPath + "/scenes/" + "default_scene.scene";
    projectJson["scene_dir"] = projectPath + "/scenes/";
    projectJson["build_dir"] = projectPath + "/build/";
    projectJson["assets_dir"] = projectPath + "/assets/";
    projectJson["src_dir"] = projectPath + "/sources/";

    std::ofstream out(projectFile, std::ios::trunc);

    if (out.is_open())
    {
        out << projectJson.dump(4);
        out.close();
    }
    else
    {
        LOG_ERROR("Failed to write project file");
        return nullptr;
    }

    auto project = new Project();

    project->setName(projectName);
    project->setFullPath(projectPath);
    project->setBuildDir(projectPath + "/build/");
    project->setSourceDir(projectPath + "/sources/");
    project->setScenesDir(projectPath + "/scenes/");
    project->setEntryScene(project->getScenesDir() + "default_scene.scene");
    project->setAssetsDir(projectPath + "/assets/");

    return project;
}

bool ProjectManager::loadConfigInProject(const std::string &configPath, Project* project)
{
    if (!std::filesystem::exists(configPath))
        return false;

    std::ifstream configFile(configPath);

    if (!configFile.is_open())
    {
        LOG_ERROR("Failed to open config file");
        return false;
    }

    nlohmann::json config;

    try
    {
        configFile >> config;
    }
    catch (const nlohmann::json::parse_error& e)
    {
        LOG_ERROR("Failed to parse config file" + std::string(e.what()));
        return false;
    }

    project->setName(config.value("name", ""));
    project->setFullPath(config.value("project_path", ""));
    project->setEntryScene(config.value("entry_scene", ""));
    project->setBuildDir(config.value("build_dir", ""));
    project->setSourceDir(config.value("src_dir", ""));
    project->setScenesDir(config.value("scene_dir", ""));
    project->setAssetsDir(config.value("assets_dir", ""));

    configFile.close();

    return true;
}

bool ProjectManager::loadProject(Project *project)
{
    const std::string scenePath = project->getEntryScene();

    if (!std::filesystem::exists(scenePath))
        return false;

    AssetsManager::instance().preLoadAllTexturesFromFolder(project->getAssetsDir() + "textures");
    AssetsManager::instance().preLoadAllModelsFromFolder(project->getAssetsDir() + "models");
    AssetsManager::instance().preLoadMaterialsFromFolder(project->getAssetsDir() + "materials");

    auto models = AssetsManager::instance().getAllStaticModels();

    for (const auto& model : models)
    {
        for (int index = 0; index < model->getMeshesSize(); index++)
        {
            const auto& mesh = model->getMesh(index);

            if (auto staticMesh = dynamic_cast<StaticMesh*>(mesh))
                staticMesh->loadFromRaw();
        }
    }

    auto skinnedModels = AssetsManager::instance().getAllSkinnedModels();

    for (const auto& skinnedModel : skinnedModels)
    {
        for (int index = 0; index < skinnedModel->getMeshesSize(); index++)
        {
            const auto& mesh = skinnedModel->getMesh(index);

            if (auto skeletalMesh = dynamic_cast<SkeletalMesh*>(mesh))
                skeletalMesh->loadFromRaw();
        }
    }

    const auto& objects = SceneManager::instance().loadObjectsFromFile(scenePath);

    auto newScene = new Scene();

    newScene->setGameObjects(objects);

    SceneManager::instance().setCurrentScene(newScene);

    return true;
}

void ProjectManager::setCurrentProject(Project *project)
{
    m_currentProject = project;
}

Project* ProjectManager::getCurrentProject()
{
    return m_currentProject;
}
