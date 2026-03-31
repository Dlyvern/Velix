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

namespace
{
    ExportTargetSelection parseExportTargetSelection(const nlohmann::json &jsonValue, ExportTargetSelection fallback)
    {
        if (!jsonValue.is_string())
            return fallback;

        std::string value = jsonValue.get<std::string>();
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });

        if (value == "linux")
            return ExportTargetSelection::Linux;
        if (value == "windows")
            return ExportTargetSelection::Windows;
        if (value == "both")
            return ExportTargetSelection::Both;
        if (value == "host")
        {
#if defined(_WIN32)
            return ExportTargetSelection::Windows;
#else
            return ExportTargetSelection::Linux;
#endif
        }

        return fallback;
    }
}

std::shared_ptr<Project> ProjectLoader::loadProject(const std::string &projectPath)
{
    if (projectPath.empty())
    {
        VX_EDITOR_ERROR_STREAM("Project path is empty\n");
        return nullptr;
    }

    const std::filesystem::path inputPath(projectPath);
    if (!std::filesystem::exists(inputPath))
    {
        VX_EDITOR_ERROR_STREAM("Project path does not exist\n");
        return nullptr;
    }

    std::string projectConfigPath;
    const std::filesystem::path projectDirectory = std::filesystem::is_directory(inputPath)
                                                       ? inputPath
                                                       : (inputPath.has_parent_path() ? inputPath.parent_path() : std::filesystem::current_path());
    const std::vector<std::string> knownProjectConfigNames = {
        "project.elixproject",
        "Project.elixproject",
        "project.elixirproject",
        "Project.elixirproject"};

    if (std::filesystem::is_regular_file(inputPath))
    {
        const std::string extension = inputPath.extension().string();
        if (extension == ".elixproject" || extension == ".elixirproject")
            projectConfigPath = inputPath.string();
    }

    for (const auto &projectConfigName : knownProjectConfigNames)
    {
        if (!projectConfigPath.empty())
            break;

        const auto candidatePath = projectDirectory / projectConfigName;
        if (!std::filesystem::exists(candidatePath))
            continue;

        projectConfigPath = candidatePath.string();
        break;
    }

    if (projectConfigPath.empty())
    {
        std::error_code iteratorError;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(
                 projectDirectory,
                 std::filesystem::directory_options::skip_permission_denied,
                 iteratorError))
        {
            if (iteratorError)
            {
                VX_EDITOR_ERROR_STREAM("Failed to scan project directory '" << projectDirectory << "': " << iteratorError.message() << '\n');
                return nullptr;
            }

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
    project->configPath = std::filesystem::path(projectConfigPath).lexically_normal().string();

    const auto rawFullPath = getConfigString({"path", "project_path"}, configDirectory.string());
    project->fullPath = makeAbsolute(rawFullPath, configDirectory);
    if (project->fullPath.empty())
        project->fullPath = configDirectory.lexically_normal().string();
    project->fullPath = resolveProjectRootPath(*project).string();

    const auto rawEntryScene = getConfigString({"scene", "entry_scene"});
    if (rawEntryScene.empty())
        VX_EDITOR_INFO_STREAM("No scene configured in project file\n");
    project->entryScene = makeAbsolute(rawEntryScene, project->fullPath);

    project->resourcesDir = makeAbsolute(getConfigString({"resources_path", "resources_dir", "assets_dir"}), project->fullPath);
    project->sourcesDir = makeAbsolute(getConfigString({"sources_path", "source_dir", "src_dir"}), project->fullPath);
    project->buildDir = makeAbsolute(getConfigString({"build_dir"}, (std::filesystem::path(project->fullPath) / "build").string()), configDirectory);
    project->scenesDir = makeAbsolute(getConfigString({"scenes_dir", "scene_dir"}), project->fullPath);
    project->exportDir = makeAbsolute(getConfigString({"export_dir"}), project->fullPath);

    if (json.contains("export") && json["export"].is_object())
    {
        const auto &exportJson = json["export"];

        auto getExportString = [&](const nlohmann::json &object, std::initializer_list<const char *> keys) -> std::string
        {
            for (const auto *key : keys)
            {
                if (!object.contains(key) || !object[key].is_string())
                    continue;

                return object[key].get<std::string>();
            }

            return {};
        };

        const std::string nestedExportDir = getExportString(exportJson, {"dir", "output_dir", "export_dir"});
        if (!nestedExportDir.empty())
            project->exportDir = makeAbsolute(nestedExportDir, project->fullPath);

        if (exportJson.contains("default_target"))
            project->exportTargetSelection = parseExportTargetSelection(exportJson["default_target"], project->exportTargetSelection);

        auto loadPlatformSettings = [&](const nlohmann::json &platformJson, ExportPlatformSettings &settings)
        {
            settings.buildDir = makeAbsolute(getExportString(platformJson, {"build_dir"}), project->fullPath);
            settings.exportDir = makeAbsolute(getExportString(platformJson, {"export_dir", "output_dir"}), project->fullPath);
            settings.cmakeGenerator = getExportString(platformJson, {"cmake_generator", "generator"});
            settings.cmakeToolchainFile = makeAbsolute(getExportString(platformJson, {"cmake_toolchain_file", "toolchain_file"}), configDirectory);
            settings.supportRootDir = makeAbsolute(getExportString(platformJson, {"support_root", "sdk_root", "runtime_root", "cmake_prefix_path"}), project->fullPath);
            settings.runtimeExecutablePath = makeAbsolute(getExportString(platformJson, {"runtime_executable", "runtime_executable_path"}), project->fullPath);
        };

        if (exportJson.contains("linux") && exportJson["linux"].is_object())
            loadPlatformSettings(exportJson["linux"], project->linuxExport);

        if (exportJson.contains("windows") && exportJson["windows"].is_object())
            loadPlatformSettings(exportJson["windows"], project->windowsExport);
    }

    if (project->resourcesDir.empty())
        project->resourcesDir = (std::filesystem::path(project->fullPath) / "resources").string();
    if (project->sourcesDir.empty())
        project->sourcesDir = (std::filesystem::path(project->fullPath) / "Sources").string();
    if (project->buildDir.empty())
        project->buildDir = (std::filesystem::path(project->fullPath) / "build").string();

    auto &assetsCache = project->cache;

    project->fullPath = resolveProjectRootPath(*project).string();

    if (!std::filesystem::exists(project->fullPath))
    {
        VX_EDITOR_ERROR_STREAM("Project full path is not correct\n");
        return nullptr;
    }

    if (!std::filesystem::is_directory(project->fullPath))
    {
        VX_EDITOR_ERROR_STREAM("Project full path is not a directory: " << project->fullPath << '\n');
        return nullptr;
    }

    engine::AssetsSerializer serializer;
    std::error_code cacheScanError;
    for (auto it = std::filesystem::recursive_directory_iterator(
             project->fullPath,
             std::filesystem::directory_options::skip_permission_denied,
             cacheScanError);
         !cacheScanError && it != std::filesystem::recursive_directory_iterator();
         ++it)
    {
        auto &entry = *it;
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

    if (cacheScanError)
    {
        VX_EDITOR_WARNING_STREAM("Failed to scan project assets in '" << project->fullPath << "': " << cacheScanError.message() << '\n');
    }

    return project;
}

ELIX_NESTED_NAMESPACE_END
