#include "ProjectManager.hpp"
#include <filesystem>
#include "VelixFlow/Logger.hpp"
#include <fstream>
#include <../libraries/json/json.hpp>
#include <VelixFlow/AssetsLoader.hpp>
#include "VelixFlow/Filesystem.hpp"
#include "Engine.hpp"
#include <filesystem>
#include <VelixFlow/Scripting/ScriptSystem.hpp>
#include <VelixFlow/BinarySerializer.hpp>

ProjectManager& ProjectManager::instance()
{
    static ProjectManager instance;

    return instance;
}

void ProjectManager::exportProjectGame()
{
    auto project = getCurrentProject();

    if(!project)
    {
        ELIX_LOG_ERROR("There is no current project");
        return;
    }

    std::string exportDir = project->exportDir + "ExportedGame/";

    std::filesystem::create_directories(exportDir);

    std::string command = "cmake -S " + project->sourceDir + " -B " + project->buildDir + " && cmake --build " + project->buildDir;

    auto result = elix::filesystem::executeCommand(command);

    ELIX_LOG_INFO(result.second);

    if (result.first != 0)
    {
        ELIX_LOG_ERROR("Build failed. Cannot export game.");
        return;
    }

    std::string userLib = project->buildDir + "/libGameLib.so";

    try
    {
        if (std::filesystem::exists(userLib))
            std::filesystem::copy_file(userLib, exportDir + "libGameLib.so", std::filesystem::copy_options::overwrite_existing);

        ELIX_LOG_INFO("Game exported successfully to: ", exportDir);
    }
    catch (const std::exception& e) 
    {
        ELIX_LOG_ERROR("Error exporting files: ", e.what());
    }

    const std::string buildFolder = "build_export";

    command = "cmake -S " + project->exportDir + " -B " + project->exportDir + "build_export/" + " && cmake --build " + project->exportDir + "build_export/";

    result = elix::filesystem::executeCommand(command);

    ELIX_LOG_INFO(result.second);

    if (result.first != 0)
    {
        ELIX_LOG_ERROR("Build failed. Cannot export game.");
        return;
    }

    std::string gameExecutable = project->exportDir + "build_export/Game";

    try
    {
        if (std::filesystem::exists(gameExecutable))
            std::filesystem::copy_file(gameExecutable, exportDir + "Game", std::filesystem::copy_options::overwrite_existing);

        ELIX_LOG_INFO("Game exported successfully to: ", exportDir);
    }
    catch (const std::exception& e) 
    {
        ELIX_LOG_ERROR("Error exporting files: ", e.what());
    }

    const std::string packetPath = exportDir + "assets.elixpacket";
    std::vector<elix::AssetModel*> models;
    elix::BinarySerializer serializer;

    for(const auto& asset : m_projectCache.getAllAssets<elix::AssetModel>())
    {
        models.push_back(asset);
    }

    serializer.writeElixPacket(packetPath, models);
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
    projectJson["export_dir"] = projectPath + "/export/";

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

    project->name = projectName;
    project->fullPath = projectPath;
    project->buildDir = projectPath + "/build/";
    project->sourceDir = projectPath + "/sources/";
    project->scenesDir = projectPath + "/scenes/";
    project->entryScene = project->scenesDir + "default_scene.scene";
    project->assetsDir = projectPath + "/assets/";
    project->exportDir = projectPath + "/export/";

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

    project->name = config.value("name", "");
    project->fullPath = config.value("project_path", "");
    project->entryScene = config.value("entry_scene", "");
    project->buildDir = config.value("build_dir", "");
    project->sourceDir = config.value("src_dir", "");
    project->scenesDir = config.value("scene_dir", "");
    project->assetsDir = config.value("assets_dir", "");
    project->exportDir = config.value("export_dir", "");

    configFile.close();

    return true;
}

bool ProjectManager::loadProject(Project* project)
{
    const std::string scenePath = project->entryScene;

    if (!std::filesystem::exists(scenePath))
        return false;

    const std::filesystem::path texturesPath = project->assetsDir + "textures";
    const std::filesystem::path modelsPath = project->assetsDir + "models";
    const std::filesystem::path materialsPath = project->assetsDir + "materials";

    for (const auto& entry : std::filesystem::directory_iterator(texturesPath))
        if (auto asset = elix::AssetsLoader::loadAsset(entry.path()))
            m_projectCache.addAsset(entry.path().string(), std::move(asset));

    ELIX_LOG_INFO("Loading models");

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

    const std::string command = "cmake -S " + project->sourceDir + " -B " + project->buildDir + " && cmake --build " + project->buildDir;

    const auto result = elix::filesystem::executeCommand(command);
    
    ELIX_LOG_INFO(result.second);

    if (result.first == 0)
    {
        // if (!std::filesystem::exists(project->getSourceDir() + "GameModule.cpp"))
        // {
        //     std::ofstream file(project->getSourceDir() + "GameModule.cpp");
        //     file << "#include \"VelixFlow/ScriptMacros.hpp\"\n"
        //         << "ELIXIR_IMPLEMENT_GAME_MODULE()\n";
        //     file.close();
        //     ELIX_LOG_WARN("Missing GameModule.cpp — recreated default one.");
        // }

        //To let loadSceneFromFile() find .so library and attach scripts successfully
        if (elix::scripting::ScriptSystem::loadLibrary(project->buildDir + "libGameLib.so"))
            project->projectLibrary = elix::scripting::ScriptSystem::getLibrary();
        else
            ELIX_LOG_WARN("Failed to load library");
    }

    // Engine::s_application->getScene()->loadSceneFromFile(scenePath, m_projectCache);

    // Engine::s_application->getRenderer()->addRenderPath<elix::render::GLShadowRender>(Engine::s_application->getScene()->getLights());

    // Engine::s_application->getRenderer()->addRenderPath<elix::render::GLUIRender>();

    // auto testShit = std::make_shared<elix::ui::UIElement>();


    // testShit->setPosition({0.0f, 0.0f}); // bottom-left corner
    // testShit->setSize({300.0f, 150.0f}); // in pixels
    // testShit->setColor({1.0f, 0.0f, 0.0f, 1.0f}); // bright red
    // testShit->setAlpha(1.0f);

    // Engine::s_application->getScene()->addUIElement(testShit);

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
