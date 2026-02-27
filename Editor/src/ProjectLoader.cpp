#include "Editor/ProjectLoader.hpp"

#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cctype>

#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Assets/AssetsSerializer.hpp"

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
    const std::filesystem::path projectDirectory = std::filesystem::path(projectPath);
    const std::vector<std::string> knownProjectConfigNames = {
        "project.elixproject",
        "Project.elixproject",
        "project.elixirproject",
        "Project.elixirproject"};

    for (const auto &projectConfigName : knownProjectConfigNames)
    {
        const auto candidatePath = projectDirectory / projectConfigName;
        if (!std::filesystem::exists(candidatePath))
            continue;

        projectConfigPath = candidatePath.string();
        break;
    }

    if (projectConfigPath.empty())
    {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(projectPath))
        {
            const auto extension = entry.path().extension().string();
            if (extension != ".elixproject" && extension != ".elixirproject")
                continue;

            projectConfigPath = entry.path().string();
            break;
        }
    }

    if (projectConfigPath.empty())
    {
        VX_EDITOR_ERROR_STREAM("Failed to find config file in project\n");
        return nullptr;
    }

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

    auto project = std::make_shared<Project>();
    const std::filesystem::path configDirectory = std::filesystem::path(projectConfigPath).parent_path();

    auto getConfigString = [&](std::initializer_list<const char *> keys, const std::string &fallback = std::string{}) -> std::string
    {
        for (const auto *key : keys)
        {
            if (!json.contains(key))
                continue;

            if (!json[key].is_string())
                continue;

            return json[key].get<std::string>();
        }

        return fallback;
    };

    auto makeAbsolute = [](const std::string &rawPath, const std::filesystem::path &basePath) -> std::string
    {
        if (rawPath.empty())
            return {};

        std::filesystem::path path(rawPath);
        if (path.is_relative())
            path = basePath / path;

        return path.lexically_normal().string();
    };

    project->name = getConfigString({"name"}, configDirectory.filename().string());

    const auto rawFullPath = getConfigString({"path", "project_path"}, projectPath);
    project->fullPath = makeAbsolute(rawFullPath, configDirectory);

    const auto rawEntryScene = getConfigString({"scene", "entry_scene"});
    if (rawEntryScene.empty())
        VX_EDITOR_INFO_STREAM("No scene configured in project file\n");
    project->entryScene = makeAbsolute(rawEntryScene, project->fullPath);

    project->resourcesDir = makeAbsolute(getConfigString({"resources_path", "resources_dir", "assets_dir"}), project->fullPath);
    project->sourcesDir = makeAbsolute(getConfigString({"sources_path", "source_dir", "src_dir"}), project->fullPath);
    project->buildDir = makeAbsolute(getConfigString({"build_dir"}, (std::filesystem::path(project->fullPath) / "build").string()), configDirectory);
    project->scenesDir = makeAbsolute(getConfigString({"scenes_dir", "scene_dir"}), project->fullPath);
    project->exportDir = makeAbsolute(getConfigString({"export_dir"}), project->fullPath);

    if (project->resourcesDir.empty())
        project->resourcesDir = (std::filesystem::path(project->fullPath) / "resources").string();
    if (project->sourcesDir.empty())
        project->sourcesDir = (std::filesystem::path(project->fullPath) / "Sources").string();
    if (project->buildDir.empty())
        project->buildDir = (std::filesystem::path(project->fullPath) / "build").string();

    auto &assetsCache = project->cache;

    if (!std::filesystem::exists(project->fullPath))
    {
        VX_EDITOR_ERROR_STREAM("Project full path is not correct\n");
        return nullptr;
    }

    engine::AssetsSerializer serializer;
    for (auto &entry : std::filesystem::recursive_directory_iterator(project->fullPath))
    {
        std::error_code fileError;
        if (!entry.is_regular_file(fileError) || fileError)
            continue;

        const auto header = serializer.readHeader(entry.path().string());
        if (!header.has_value() ||
            static_cast<engine::Asset::AssetType>(header->type) != engine::Asset::AssetType::TEXTURE)
            continue;

        const std::string relativeTexturePath = std::filesystem::relative(entry.path(), project->fullPath).lexically_normal().string();
        // VX_EDITOR_INFO_STREAM("Found texture asset: " << relativeTexturePath);

        TextureAssetRecord texture;
        texture.path = relativeTexturePath;
        assetsCache.texturesByPath[texture.path] = texture;
    }

    return project;
}

ELIX_NESTED_NAMESPACE_END
