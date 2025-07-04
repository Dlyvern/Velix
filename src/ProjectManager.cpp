#include "ProjectManager.hpp"
#include <filesystem>
#include "ElixirCore/Logger.hpp"
#include <fstream>
#include <../libraries/json/json.hpp>
#include <ElixirCore/AssetsLoader.hpp>
#include "ElixirCore/Filesystem.hpp"
#include "Engine.hpp"
#include <ElixirCore/ShadowRender.hpp>
#include <ElixirCore/LightManager.hpp>

ProjectManager & ProjectManager::instance()
{
    static ProjectManager instance;

    return instance;
}

Project* ProjectManager::createProject(const std::string& projectName, const std::string& projectPath)
{
    if (std::filesystem::exists(projectPath))
    {
        ELIX_LOG_WARN("Path already exists");
        return nullptr;
    }

    std::filesystem::create_directories(projectPath);

    std::string templatePath = "../ProjectTemplate";

    if (!std::filesystem::exists(templatePath))
    {
        ELIX_LOG_ERROR("There is no template");
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
        ELIX_LOG_ERROR("Failed to write project file");
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
        ELIX_LOG_ERROR("Failed to open config file");
        return false;
    }

    nlohmann::json config;

    try
    {
        configFile >> config;
    }
    catch (const nlohmann::json::parse_error& e)
    {
        ELIX_LOG_ERROR("Failed to parse config file %s", std::string(e.what()));
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

bool ProjectManager::loadProject(Project* project)
{
    const std::string scenePath = project->getEntryScene();

    if (!std::filesystem::exists(scenePath))
        return false;

    const std::filesystem::path texturesPath = project->getAssetsDir() + "textures";
    const std::filesystem::path modelsPath = project->getAssetsDir() + "models";
    const std::filesystem::path materialsPath = project->getAssetsDir() + "materials";

    for (const auto& entry : std::filesystem::directory_iterator(texturesPath))
        if (auto asset = elix::AssetsLoader::loadAsset(entry.path()))
            m_projectCache.addAsset(entry.path().string(), std::move(asset));

    for (const auto& entry : std::filesystem::directory_iterator(modelsPath))
        if (auto asset = elix::AssetsLoader::loadAsset(entry.path()))
        {
            auto model = m_projectCache.addAsset(entry.path().string(), std::move(asset));

            if (auto animation = elix::AssetsLoader::loadAsset<elix::AssetAnimation>(entry.path()))
            {
                dynamic_cast<elix::AssetModel*>(model)->getModel()->addAnimation(animation->getAnimation());

                const std::string animationPath = animation->getAnimation()->name;

                m_projectCache.addAsset(animationPath, std::move(animation));
            }

        }

    for (const auto& entry : std::filesystem::directory_iterator(materialsPath))
        if (auto asset  = elix::AssetsLoader::loadAsset(entry.path(), &m_projectCache))
            m_projectCache.addAsset(entry.path().string(), std::move(asset));

    auto folderTextureAsset = elix::AssetsLoader::loadAsset(elix::filesystem::getExecutablePath().string() + "/resources/textures/folder.png");
    m_projectCache.addAsset(elix::filesystem::getExecutablePath().string() + "/resources/textures/folder.png", std::move(folderTextureAsset));

    auto fileTextureAsset = elix::AssetsLoader::loadAsset(elix::filesystem::getExecutablePath().string() + "/resources/textures/file.png");
    m_projectCache.addAsset(elix::filesystem::getExecutablePath().string() + "/resources/textures/file.png", std::move(fileTextureAsset));

    Engine::s_application->getScene()->loadSceneFromFile(scenePath, m_projectCache);

    Engine::s_application->getRenderer()->addRenderPath<elix::ShadowRender>(Engine::s_application->getScene()->getLights());

    return true;
}

void ProjectManager::setCurrentProject(Project *project)
{
    m_currentProject = project;
}

Project* ProjectManager::getCurrentProject() const
{
    return m_currentProject;
}

elix::AssetsCache* ProjectManager::getAssetsCache()
{
    return &m_projectCache;
}
