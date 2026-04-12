#include "Editor/Runtime/EditorRuntime.hpp"

#include "Editor/Editor.hpp"
#include "Editor/PluginSystem/EditorPluginRegistry.hpp"
#include "VelixSDK/EditorPlugin.hpp"
#include "Editor/ImGuiRenderGraphPass.hpp"
#include "Editor/Project.hpp"
#include "Editor/ProjectLoader.hpp"
#include "Editor/EditorResourcesStorage.hpp"

#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Runtime/EngineConfig.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Components/CameraComponent.hpp"
#include "Engine/Components/ParticleSystemComponent.hpp"
#include "Engine/Components/ReflectionProbeComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Components/ScriptComponent.hpp"
#include "Engine/DebugDraw.hpp"
#include "Engine/Diagnostics.hpp"
#include "Engine/PluginSystem/PluginLoader.hpp"
#include "Engine/PluginSystem/PluginManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Scripting/ScriptsRegister.hpp"
#include "Engine/Scripting/VelixAPI.hpp"

#include "Core/VulkanContext.hpp"

#include "Core/Logger.hpp"

#include <backends/imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <volk.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <unordered_set>

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace
{
    template <typename TValue>
    void hashCombine(size_t &seed, const TValue &value)
    {
        seed ^= std::hash<TValue>{}(value) + 0x9e3779b97f4a7c15ULL + (seed << 6u) + (seed >> 2u);
    }

    std::string sharedLibraryExtension()
    {
#if defined(_WIN32)
        return ".dll";
#elif defined(__linux__)
        return ".so";
#else
        return "";
#endif
    }

    std::string toLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    VkExtent2D makeScaledRenderExtent(uint32_t width, uint32_t height)
    {
        const float renderScale = std::clamp(elix::engine::RenderQualitySettings::getInstance().renderScale, 0.25f, 2.0f);
        const uint32_t baseWidth = std::max(width, 1u);
        const uint32_t baseHeight = std::max(height, 1u);

        const uint32_t scaledWidth = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<double>(baseWidth) * renderScale)));
        const uint32_t scaledHeight = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<double>(baseHeight) * renderScale)));
        return VkExtent2D{scaledWidth, scaledHeight};
    }

    int buildArtifactPreference(const std::filesystem::path &path)
    {
        for (const auto &segment : path)
        {
            const std::string loweredSegment = toLowerCopy(segment.string());
            if (loweredSegment == "release")
                return 0;
            if (loweredSegment == "relwithdebinfo")
                return 1;
            if (loweredSegment == "minsizerel")
                return 2;
            if (loweredSegment == "debug")
                return 4;
        }

        return 3;
    }

    bool isHotReloadLibraryPath(const std::filesystem::path &path)
    {
        return path.filename().string().find(".__velix_hot_reload__") != std::string::npos;
    }

    bool isGameModuleLibraryCandidate(const std::filesystem::path &path)
    {
        if (isHotReloadLibraryPath(path))
            return false;

        const std::string libraryExtension = sharedLibraryExtension();
        if (libraryExtension.empty() || path.extension() != libraryExtension)
            return false;

        const std::string stem = path.stem().string();
        if (stem == "GameModule" || stem == "libGameModule")
            return true;

        return path.filename().string().find("GameModule") != std::string::npos;
    }

    template <typename TFunction>
    void forEachScriptComponent(const std::shared_ptr<elix::engine::Scene> &scene, TFunction &&function)
    {
        if (!scene)
            return;

        std::vector<elix::engine::Entity::SharedPtr> entitiesSnapshot;
        entitiesSnapshot.reserve(scene->getEntities().size());
        for (const auto &entity : scene->getEntities())
            entitiesSnapshot.push_back(entity);

        for (const auto &entity : entitiesSnapshot)
        {
            if (!entity)
                continue;

            for (auto *scriptComponent : entity->getComponents<elix::engine::ScriptComponent>())
                function(scriptComponent);
        }
    }

    void releaseScriptInstancesForScenes(const std::initializer_list<std::shared_ptr<elix::engine::Scene>> &scenes)
    {
        std::unordered_set<const elix::engine::Scene *> visitedScenes;

        for (const auto &scene : scenes)
        {
            if (!scene || !visitedScenes.insert(scene.get()).second)
                continue;

            forEachScriptComponent(scene, [](elix::engine::ScriptComponent *scriptComponent)
            {
                if (scriptComponent)
                    scriptComponent->releaseScriptInstance();
            });
        }
    }

    bool isPreferredLibraryCandidate(const std::filesystem::path &candidatePath,
                                     const std::filesystem::path &bestPath)
    {
        if (candidatePath.empty())
            return false;

        if (bestPath.empty())
            return true;

        std::error_code candidateError;
        std::error_code bestError;
        const auto candidateWriteTime = std::filesystem::last_write_time(candidatePath, candidateError);
        const auto bestWriteTime = std::filesystem::last_write_time(bestPath, bestError);

        const bool hasCandidateWriteTime = !candidateError;
        const bool hasBestWriteTime = !bestError;

        if (hasCandidateWriteTime && hasBestWriteTime && candidateWriteTime != bestWriteTime)
            return candidateWriteTime > bestWriteTime;

        if (hasCandidateWriteTime != hasBestWriteTime)
            return hasCandidateWriteTime;

        const int candidatePreference = buildArtifactPreference(candidatePath);
        const int bestPreference = buildArtifactPreference(bestPath);
        if (candidatePreference != bestPreference)
            return candidatePreference < bestPreference;

        return candidatePath.string() < bestPath.string();
    }

    bool renderGraphUsesRTShadows(const elix::engine::RenderQualitySettings &settings,
                                  const std::shared_ptr<elix::core::VulkanContext> &context)
    {
        const bool supportsRayQuery = context && context->hasRayQuerySupport();
        const bool supportsRayPipeline = context && context->hasRayTracingPipelineSupport();
        const bool supportsAnyRT = supportsRayQuery || supportsRayPipeline;
        return settings.enableRayTracing && settings.enableRTShadows && supportsAnyRT;
    }

    bool renderGraphUsesRTAO(const elix::engine::RenderQualitySettings &settings,
                             const std::shared_ptr<elix::core::VulkanContext> &context)
    {
        return settings.enableRayTracing && settings.enableRTAO && context && context->hasRayQuerySupport();
    }

    bool renderGraphUsesSSAO(const elix::engine::RenderQualitySettings &settings,
                             const std::shared_ptr<elix::core::VulkanContext> &context)
    {
        if (!settings.enablePostProcessing)
            return false;

        return settings.enableSSAO || settings.enableGTAO || renderGraphUsesRTAO(settings, context);
    }

    bool renderGraphUsesRTReflections(const elix::engine::RenderQualitySettings &settings,
                                      const std::shared_ptr<elix::core::VulkanContext> &context)
    {
        const bool supportsRayQuery = context && context->hasRayQuerySupport();
        const bool supportsRayPipeline = context && context->hasRayTracingPipelineSupport();
        const bool supportsAnyRT = supportsRayQuery || supportsRayPipeline;
        return settings.enableRayTracing && settings.enableRTReflections && supportsAnyRT;
    }

    bool renderGraphUsesRTGI(const elix::engine::RenderQualitySettings &settings,
                             const std::shared_ptr<elix::core::VulkanContext> &context)
    {
        const bool supportsRayQuery = context && context->hasRayQuerySupport();
        const bool supportsRayPipeline = context && context->hasRayTracingPipelineSupport();
        const bool supportsAnyRT = supportsRayQuery || supportsRayPipeline;
        return settings.enableRayTracing && settings.enableRTGI && supportsAnyRT;
    }

    bool renderGraphUsesContactShadows(const elix::engine::RenderQualitySettings &settings)
    {
        return settings.enablePostProcessing && settings.enableContactShadows;
    }

    bool renderGraphUsesSSR(const elix::engine::RenderQualitySettings &settings)
    {
        return settings.enablePostProcessing && settings.enableSSR;
    }

    bool renderGraphUsesBloom(const elix::engine::RenderQualitySettings &settings)
    {
        return settings.enablePostProcessing && settings.enableBloom;
    }

    bool renderGraphUsesVolumetricFog(const elix::engine::RenderQualitySettings &settings,
                                      const elix::engine::Scene *scene)
    {
        if (settings.volumetricFogQuality == elix::engine::RenderQualitySettings::VolumetricFogQuality::Off)
            return false;

        if (settings.overrideVolumetricFogSceneSetting)
            return settings.volumetricFogOverrideEnabled && scene;

        return scene && scene->getFogSettings().enabled;
    }

    // Returns false when RT shadows are fully active and volumetric fog is not in use,
    // allowing the raster shadow pass to be skipped to save GPU work.
    bool renderGraphUsesRasterShadows(const elix::engine::RenderQualitySettings &settings,
                                      const std::shared_ptr<elix::core::VulkanContext> &context,
                                      const elix::engine::Scene *scene)
    {
        // Volumetric fog reads cascade shadow maps directly — keep raster shadows alive.
        if (renderGraphUsesVolumetricFog(settings, scene))
            return true;
        // RT shadows fully replace raster shadows.
        if (renderGraphUsesRTShadows(settings, context))
            return false;
        return true;
    }

    elix::engine::RenderQualitySettings::AntiAliasingMode renderGraphAntiAliasingMode(
        const elix::engine::RenderQualitySettings &settings)
    {
        return settings.enablePostProcessing
                   ? settings.getAntiAliasingMode()
                   : elix::engine::RenderQualitySettings::AntiAliasingMode::NONE;
    }

    bool renderGraphUsesCinematicEffects(const elix::engine::RenderQualitySettings &settings)
    {
        return settings.enablePostProcessing &&
               (settings.enableVignette || settings.enableFilmGrain || settings.enableChromaticAberration);
    }

    bool renderGraphUsesMotionBlur(const elix::engine::RenderQualitySettings &settings)
    {
        return settings.enablePostProcessing && settings.enableMotionBlur;
    }

    void syncNearestReflectionProbe(elix::engine::renderGraph::LightingRenderGraphPass *lightingPass,
                                    const elix::engine::Scene *scene,
                                    const std::shared_ptr<elix::engine::Camera> &camera)
    {
        if (!lightingPass)
            return;

        if (!scene)
        {
            lightingPass->setProbeData(VK_NULL_HANDLE, VK_NULL_HANDLE, {}, 0.0f, 0.0f);
            return;
        }

        const glm::vec3 cameraPos = camera ? camera->getPosition() : glm::vec3(0.0f);

        elix::engine::ReflectionProbeComponent *bestProbe = nullptr;
        glm::vec3 bestProbePos{0.0f};
        float bestEdgeDistance = std::numeric_limits<float>::max();
        float bestCenterDistance = std::numeric_limits<float>::max();

        for (const auto &entity : scene->getEntities())
        {
            if (!entity || !entity->isEnabled())
                continue;

            auto *probe = entity->getComponent<elix::engine::ReflectionProbeComponent>();
            auto *transform = entity->getComponent<elix::engine::Transform3DComponent>();
            if (!probe || !transform || !probe->hasCubemap())
                continue;

            const glm::vec3 probePos = transform->getWorldPosition();
            const float centerDistance = glm::length(cameraPos - probePos);
            const float radius = std::max(probe->radius, 0.0f);
            const float edgeDistance = std::max(centerDistance - radius, 0.0f);

            if (edgeDistance < bestEdgeDistance ||
                (edgeDistance == bestEdgeDistance && centerDistance < bestCenterDistance))
            {
                bestProbe = probe;
                bestProbePos = probePos;
                bestEdgeDistance = edgeDistance;
                bestCenterDistance = centerDistance;
            }
        }

        if (bestProbe)
        {
            lightingPass->setProbeData(
                bestProbe->getProbeEnvView(),
                bestProbe->getProbeEnvSampler(),
                bestProbePos,
                bestProbe->radius,
                bestProbe->intensity);
            return;
        }

        lightingPass->setProbeData(VK_NULL_HANDLE, VK_NULL_HANDLE, {}, 0.0f, 0.0f);
    }

    bool renderGraphUsesParticles(const elix::engine::Scene *scene)
    {
        if (!scene)
            return false;

        for (const auto &entity : scene->getEntities())
        {
            if (!entity || !entity->isEnabled())
                continue;

            for (auto *particleSystemComponent : entity->getComponents<elix::engine::ParticleSystemComponent>())
            {
                if (!particleSystemComponent)
                    continue;

                const auto *particleSystem = particleSystemComponent->getParticleSystem();
                if (!particleSystem)
                    continue;

                if (particleSystem->isPlaying())
                    return true;

                for (const auto &emitter : particleSystem->getEmitters())
                {
                    if (emitter && emitter->enabled && emitter->getAliveCount() > 0u)
                        return true;
                }
            }
        }

        return false;
    }

    bool renderGraphUsesUI(const elix::engine::Scene *scene)
    {
        if (!scene)
            return false;

        for (const auto &text : scene->getUITexts())
            if (text && text->isEnabled())
                return true;

        for (const auto &button : scene->getUIButtons())
            if (button && button->isEnabled())
                return true;

        for (const auto &billboard : scene->getBillboards())
            if (billboard && billboard->isEnabled())
                return true;

        return false;
    }

    bool editorRenderGraphUsesSelection(const elix::editor::Editor *editor)
    {
        return editor && editor->getSelectedEntityIdForBuffer() != 0u;
    }

    bool renderGraphUsesDebugOverlay()
    {
        return elix::engine::DebugDraw::hasShapes();
    }

    size_t baseRenderGraphTopologyHash(const elix::engine::Scene *scene)
    {
        const auto &settings = elix::engine::RenderQualitySettings::getInstance();
        const auto context = elix::core::VulkanContext::getContext();
        const VkSampleCountFlagBits effectiveMsaaSamples = context
            ? context->getEffectiveMsaaSampleCount(settings.getRequestedMsaaSampleCount())
            : VK_SAMPLE_COUNT_1_BIT;

        size_t seed = 0u;
        hashCombine(seed, renderGraphUsesSSAO(settings, context));
        hashCombine(seed, renderGraphUsesRTShadows(settings, context));
        hashCombine(seed, renderGraphUsesRTAO(settings, context));
        hashCombine(seed, renderGraphUsesContactShadows(settings));
        hashCombine(seed, renderGraphUsesRTReflections(settings, context));
        hashCombine(seed, renderGraphUsesRTGI(settings, context));
        hashCombine(seed, renderGraphUsesRTGI(settings, context) && settings.enableRTGIDenoiser);
        hashCombine(seed, renderGraphUsesRasterShadows(settings, context, scene));
        hashCombine(seed, renderGraphUsesSSR(settings));
        hashCombine(seed, renderGraphUsesVolumetricFog(settings, scene));
        hashCombine(seed, static_cast<uint32_t>(settings.volumetricFogQuality));
        hashCombine(seed, settings.overrideVolumetricFogSceneSetting);
        hashCombine(seed, settings.volumetricFogOverrideEnabled);
        hashCombine(seed, renderGraphUsesBloom(settings));
        hashCombine(seed, static_cast<uint32_t>(effectiveMsaaSamples));
        hashCombine(seed, static_cast<uint32_t>(renderGraphAntiAliasingMode(settings)));
        hashCombine(seed, renderGraphUsesCinematicEffects(settings));
        hashCombine(seed, renderGraphUsesMotionBlur(settings));
        hashCombine(seed, renderGraphUsesUI(scene));
        return seed;
    }

    size_t editorRenderGraphTopologyHash(const elix::engine::Scene *scene,
                                         const elix::editor::Editor *editor)
    {
        size_t seed = baseRenderGraphTopologyHash(scene);
        hashCombine(seed, editorRenderGraphUsesSelection(editor));
        return seed;
    }

    size_t gameViewportRenderGraphTopologyHash(const elix::engine::Scene *scene)
    {
        size_t seed = baseRenderGraphTopologyHash(scene);
        return seed;
    }

    std::filesystem::path findGameModuleLibraryPath(const std::filesystem::path &buildDirectory)
    {
        if (buildDirectory.empty() || !std::filesystem::exists(buildDirectory))
            return {};

        std::filesystem::path bestMatch;

        for (const auto &entry : std::filesystem::recursive_directory_iterator(buildDirectory))
        {
            if (!entry.is_regular_file())
                continue;

            const auto path = entry.path();
            if (!isGameModuleLibraryCandidate(path))
                continue;

            if (isPreferredLibraryCandidate(path, bestMatch))
                bestMatch = path;
        }

        return bestMatch;
    }

    struct LoadedProjectLibrary
    {
        elix::engine::LibraryHandle handle{nullptr};
        std::filesystem::path loadedPath;
        bool isTemporaryCopy{false};
    };

    LoadedProjectLibrary loadProjectLibrary(const std::filesystem::path &sourceLibraryPath)
    {
        LoadedProjectLibrary loadedLibrary;
        if (sourceLibraryPath.empty() || !std::filesystem::exists(sourceLibraryPath))
            return loadedLibrary;

        const auto timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const std::filesystem::path temporaryCopyPath =
            sourceLibraryPath.parent_path() /
            (sourceLibraryPath.stem().string() + ".__velix_hot_reload__" + std::to_string(timestamp) + sourceLibraryPath.extension().string());

        std::error_code copyError;
        std::filesystem::copy_file(sourceLibraryPath, temporaryCopyPath, std::filesystem::copy_options::overwrite_existing, copyError);
        if (!copyError)
        {
            loadedLibrary.handle = elix::engine::PluginLoader::loadLibrary(temporaryCopyPath);
            if (loadedLibrary.handle)
            {
                loadedLibrary.loadedPath = temporaryCopyPath;
                loadedLibrary.isTemporaryCopy = true;
                return loadedLibrary;
            }

            std::error_code removeError;
            std::filesystem::remove(temporaryCopyPath, removeError);
        }
        else
        {
            VX_EDITOR_WARNING_STREAM("Failed to create hot reload copy for game module: " << temporaryCopyPath << " (" << copyError.message() << ")\n");
        }

        loadedLibrary.handle = elix::engine::PluginLoader::loadLibrary(sourceLibraryPath);
        if (loadedLibrary.handle)
            loadedLibrary.loadedPath = sourceLibraryPath;

        return loadedLibrary;
    }

    bool tryLoadProjectScriptsModule(elix::editor::Project &project,
                                     elix::engine::ScriptsRegister *&outScriptsRegister,
                                     std::string &outModulePath)
    {
        outScriptsRegister = nullptr;
        outModulePath.clear();

        const std::filesystem::path buildDirectory = project.buildDir.empty()
                                                         ? std::filesystem::path(project.fullPath) / "build"
                                                         : std::filesystem::path(project.buildDir);

        const std::filesystem::path moduleLibraryPath = findGameModuleLibraryPath(buildDirectory);
        if (moduleLibraryPath.empty())
        {
            elix::engine::ScriptsRegister::setActiveRegister(nullptr);
            VX_EDITOR_WARNING_STREAM("GameModule not found in build directory. Scripts will remain unavailable until module is built.\n");
            return false;
        }

        auto loadedProjectLibrary = loadProjectLibrary(moduleLibraryPath);
        if (!loadedProjectLibrary.handle)
        {
            elix::engine::ScriptsRegister::setActiveRegister(nullptr);
            VX_EDITOR_ERROR_STREAM("Failed to load game module on startup: " << moduleLibraryPath << '\n');
            return false;
        }

        auto getScriptsRegisterFunction = elix::engine::PluginLoader::getFunction<elix::engine::ScriptsRegister &(*)()>(
            "getScriptsRegister",
            loadedProjectLibrary.handle);
        if (!getScriptsRegisterFunction)
        {
            if (loadedProjectLibrary.isTemporaryCopy)
            {
                std::error_code removeError;
                elix::engine::PluginLoader::closeLibrary(loadedProjectLibrary.handle);
                std::filesystem::remove(loadedProjectLibrary.loadedPath, removeError);
            }
            else
                elix::engine::PluginLoader::closeLibrary(loadedProjectLibrary.handle);

            elix::engine::ScriptsRegister::setActiveRegister(nullptr);
            VX_EDITOR_ERROR_STREAM("Game module loaded but getScriptsRegister symbol is missing: " << moduleLibraryPath << '\n');
            return false;
        }

        if (project.projectLibrary)
            project.unloadProjectLibrary();

        project.rememberLoadedProjectLibrary(loadedProjectLibrary.handle,
                                             loadedProjectLibrary.loadedPath,
                                             loadedProjectLibrary.isTemporaryCopy);
        outScriptsRegister = &getScriptsRegisterFunction();
        outModulePath = moduleLibraryPath.string();
        elix::engine::ScriptsRegister::setActiveRegister(outScriptsRegister);

        VX_EDITOR_INFO_STREAM("Loaded " << outScriptsRegister->getScripts().size() << " script(s) from " << outModulePath << '\n');
        return true;
    }
} // namespace

ELIX_NESTED_NAMESPACE_BEGIN(editor)

EditorRuntime::EditorRuntime(const engine::ApplicationConfig &config)
{
    if (config.getArgsSize() < 2)
    {
        VX_DEV_ERROR_STREAM("Usage: Velix <project-path>\n");
        throw std::runtime_error("Failed to init editor runtime");
    }

    const std::filesystem::path inputProjectPath = std::filesystem::absolute(config.getArgs()[1]).lexically_normal();
    m_projectPath = isProjectConfigFilePath(inputProjectPath)
                        ? inputProjectPath.parent_path().string()
                        : inputProjectPath.string();
}

void EditorRuntime::setLoadingStatus(std::string status)
{
    std::scoped_lock lock(m_loadingStatusMutex);
    m_loadingStatus = std::move(status);
}

std::string EditorRuntime::getLoadingStatus() const
{
    std::scoped_lock lock(m_loadingStatusMutex);
    return m_loadingStatus;
}

void EditorRuntime::setLoadingWindowDecorationsVisible(bool visible)
{
    const auto vulkanContext = core::VulkanContext::getContext();
    if (!vulkanContext)
        return;

    const auto swapchain = vulkanContext->getSwapchain();
    if (!swapchain)
        return;

    auto &window = swapchain->getWindow();
    GLFWwindow *windowHandle = window.getRawHandler();

    if (!visible)
    {
        window.setFullscreen(false);

        if (windowHandle)
            glfwRestoreWindow(windowHandle);

        int currentWidth = 0;
        int currentHeight = 0;
        window.getSize(&currentWidth, &currentHeight);

        if (currentWidth > 0 && currentHeight > 0)
        {
            m_loadingPreviousWindowWidth = currentWidth;
            m_loadingPreviousWindowHeight = currentHeight;
            m_loadingWindowSizeCaptured = true;
        }

        window.setShowDecorations(false);
        window.setSize(800, 600);
        window.centerizedOnScreen();
    }
    else
    {
        window.setShowDecorations(true);
        m_loadingWindowSizeCaptured = false;
    }

    m_loadingDecorationsHidden = !visible;
}

bool EditorRuntime::init()
{
#ifdef _WIN32
    {
        const auto context = core::VulkanContext::getContext();
        volkInitialize();
        volkLoadInstance(context->getInstance());
        volkLoadDevice(context->getDevice()->vk());
    }
#endif

    m_project = ProjectLoader::loadProject(m_projectPath);

    if (!m_project)
    {
        VX_DEV_ERROR_STREAM("Failed to load project\n");
        return false;
    }

    const std::filesystem::path projectRoot = resolveProjectRootPath(*m_project);
    engine::AssetsLoader::setTextureAssetImportRootDirectory(projectRoot);

    engine::ScriptsRegister *startupScriptsRegister = nullptr;
    std::string startupModulePath;
    tryLoadProjectScriptsModule(*m_project, startupScriptsRegister, startupModulePath);

    {
        const std::filesystem::path execDir = engine::diagnostics::getExecutableDirectory();
        const std::filesystem::path projectDir = projectRoot;
        auto &pm = engine::PluginManager::instance();
        // Engine plugins: always loaded, cannot be disabled by the user.
        pm.loadPluginsFromDirectory(execDir / "resources" / "plugins", engine::PluginManager::PluginCategory::Engine);
        pm.loadPluginsFromDirectory(execDir / "Plugins", engine::PluginManager::PluginCategory::Engine);
        // Custom plugins: per-project, user can enable/disable via EngineConfig.
        pm.loadPluginsFromDirectory(projectDir / "Plugins", engine::PluginManager::PluginCategory::Custom);

        // Register any loaded plugins that also implement IEditorPlugin.
        auto &epr = editor::EditorPluginRegistry::instance();
        epr.unregisterAll();
        const auto &loadedPlugins = pm.getLoadedPlugins();
        VX_EDITOR_INFO_STREAM("[EditorRuntime] Total loaded plugins: " << loadedPlugins.size() << '\n');
        for (auto *plugin : loadedPlugins)
        {
            if (auto *ep = dynamic_cast<elix::sdk::IEditorPlugin *>(plugin))
            {
                VX_EDITOR_INFO_STREAM("[EditorRuntime] Registered IEditorPlugin: " << plugin->getName() << '\n');
                epr.registerEditorPlugin(ep);
            }
            else
            {
                VX_EDITOR_INFO_STREAM("[EditorRuntime] Plugin '" << plugin->getName()
                                      << "' is NOT an IEditorPlugin (dynamic_cast failed)\n");
            }
        }
        VX_EDITOR_INFO_STREAM("[EditorRuntime] Editor plugins registered: "
                              << epr.getPlugins().size() << '\n');
    }

    m_editorScene = std::make_shared<engine::Scene>();
    m_activeScene = m_editorScene;
    m_stillLoadingTheScene = true;

    const std::string entryScenePath = m_project->entryScene;
    m_pendingEditorScenePath = std::filesystem::path(entryScenePath).lexically_normal();
    m_loadingStartedAt = std::chrono::steady_clock::now();
    m_loadingSceneName = std::filesystem::path(entryScenePath).filename().string();
    if (m_loadingSceneName.empty())
        m_loadingSceneName = entryScenePath;
    setLoadingStatus("Preparing async scene loading...");

    m_loadingFuture = std::async(std::launch::async, [this, entryScenePath]() -> engine::Scene::SharedPtr
                                 {
                                    std::this_thread::sleep_for(std::chrono::seconds(1));
                                     auto scene = std::make_shared<engine::Scene>();
                                     if (!scene->loadSceneFromFile(entryScenePath,
                                                                   [this](const std::string &status)
                                                                   { setLoadingStatus(status); }))
                                     {
                                         setLoadingStatus("Failed to load scene");
                                         VX_EDITOR_ERROR_STREAM("Failed to load scene asynchronously: " << entryScenePath << '\n');
                                         return nullptr;
                                     }
                                     setLoadingStatus("Scene is ready");
                                     return scene; });

    m_shaderHotReloader = std::make_unique<engine::shaders::ShaderHotReloader>("./resources/shaders");

    m_editor = std::make_shared<Editor>();
    m_editor->refreshPluginsWindow();
    m_editor->setDockingFullscreen(true);
    m_editor->setReloadShadersCallback([this](std::vector<std::string> *outErrors) -> size_t
                                       { return m_shaderHotReloader ? m_shaderHotReloader->recompileAll(outErrors) : 0; });
    m_editor->setScriptReloadScenesProvider([this]()
                                            {
                                                std::vector<engine::Scene::SharedPtr> scenes;
                                                scenes.reserve(2);
                                                if (m_editorScene)
                                                    scenes.push_back(m_editorScene);
                                                if (m_playScene)
                                                    scenes.push_back(m_playScene);
                                                return scenes;
                                            });
    // Must be set BEFORE initEditorRenderGraph(), which triggers initStyle() → creates m_assetsWindow.
    m_editor->setOnSceneOpenRequest([this](const std::filesystem::path &path)
                                    { openSceneFromFile(path); });

    initEditorRenderGraph();
    m_editorRenderGraphTopologyHash = editorRenderGraphTopologyHash(m_activeScene.get(), m_editor.get());

    applyEditorViewportExtent(m_editor->getViewportX(), m_editor->getViewportY());
    applyGameViewportExtent(m_editor->getGameViewportX(), m_editor->getGameViewportY());

    m_editor->setScene(m_activeScene);
    m_editor->setProject(m_project);
    m_editor->setCurrentScenePath(m_pendingEditorScenePath);
    m_editor->setProjectScriptsRegister(startupScriptsRegister, startupModulePath);
    m_editorRenderCamera = m_editor->getCurrentCamera();
    if (m_editorRenderCamera)
        m_editorRenderCamera->setPosition({0.0f, 1.0f, 5.0f});

    switchActiveScene(m_editorScene);

    m_editor->addOnModeChangedCallback(std::bind(&EditorRuntime::onEditorModeChanged, this, std::placeholders::_1));

    m_editor->setProbeCaptureCallback([this](engine::Entity *entity)
    {
        m_pendingProbeCaptureEntity = entity;
    });

    return true;
}

void EditorRuntime::onEditorModeChanged(Editor::EditorMode mode)
{
    auto forEachScriptComponent = [](const std::shared_ptr<engine::Scene> &scene, auto &&function)
    {
        if (!scene)
            return;

        std::vector<engine::Entity::SharedPtr> entitiesSnapshot;
        entitiesSnapshot.reserve(scene->getEntities().size());
        for (const auto &entity : scene->getEntities())
            entitiesSnapshot.push_back(entity);

        for (const auto &entity : entitiesSnapshot)
        {
            if (!entity)
                continue;

            for (auto *scriptComponent : entity->getComponents<engine::ScriptComponent>())
                function(scriptComponent);
        }
    };

    if (mode == Editor::EditorMode::PLAY)
    {
        if (!m_isPlaySessionActive)
        {
            m_playScene = m_editorScene ? m_editorScene->copy() : nullptr;
            if (!m_playScene)
            {
                VX_EDITOR_ERROR_STREAM("Failed to create play scene copy.\n");
                m_shouldUpdate = false;
                return;
            }

            switchActiveScene(m_playScene);
            forEachScriptComponent(m_activeScene, [](engine::ScriptComponent *scriptComponent)
                                   {
                                                                              if (scriptComponent)
                                                                                  scriptComponent->onAttach(); });
            m_isPlaySessionActive = true;
        }

        m_shouldUpdate = true;
    }
    else if (mode == Editor::EditorMode::PAUSE)
    {
        m_shouldUpdate = false;
    }
    else if (mode == Editor::EditorMode::EDIT)
    {
        if (m_isPlaySessionActive)
        {
            forEachScriptComponent(m_activeScene, [](engine::ScriptComponent *scriptComponent)
                                   {
                                                                              if (scriptComponent)
                                                                                  scriptComponent->onDetach(); });

            m_playScene.reset();
            m_isPlaySessionActive = false;
        }

        switchActiveScene(m_editorScene);
        m_shouldUpdate = false;
    }
}

void EditorRuntime::switchActiveScene(const std::shared_ptr<engine::Scene> &scene)
{
    if (!scene)
        return;

    m_activeScene = scene;
    engine::scripting::setActiveScene(m_activeScene.get());
    m_editor->setScene(m_activeScene);

    if (m_particleRenderGraphPass)
        m_particleRenderGraphPass->setScene(m_activeScene.get());
    if (m_gameParticleRenderGraphPass)
        m_gameParticleRenderGraphPass->setScene(m_activeScene.get());
    if (m_sprite2DRenderGraphPass)
        m_sprite2DRenderGraphPass->setScene(m_activeScene.get());
    if (m_gameSprite2DRenderGraphPass)
        m_gameSprite2DRenderGraphPass->setScene(m_activeScene.get());
    if (m_decalRenderGraphPass)
        m_decalRenderGraphPass->setScene(m_activeScene.get());
    if (m_gameDecalRenderGraphPass)
        m_gameDecalRenderGraphPass->setScene(m_activeScene.get());
    if (m_editorBillboardRenderGraphPass)
        m_editorBillboardRenderGraphPass->setScene(m_activeScene);

    m_gameRenderCamera = nullptr;
    for (const auto &entity : m_activeScene->getEntities())
    {
        if (!entity || !entity->isEnabled())
            continue;

        if (auto *cameraComponent = entity->getComponent<engine::CameraComponent>())
        {
            m_gameRenderCamera = cameraComponent->getCamera();
            break;
        }
    }
}

void EditorRuntime::openSceneFromFile(const std::filesystem::path &path)
{
    if (m_isPlaySessionActive)
        return;

    if (m_loadingFuture.valid() && m_stillLoadingTheScene)
        return;

    const std::string scenePath = path.string();
    m_pendingEditorScenePath = path.lexically_normal();
    m_stillLoadingTheScene = true;
    m_loadingStartedAt = std::chrono::steady_clock::now();
    m_loadingSceneName = path.filename().string();
    setLoadingStatus("Preparing async scene loading...");

    m_loadingFuture = std::async(std::launch::async, [this, scenePath]() -> engine::Scene::SharedPtr
                                 {
                                     auto scene = std::make_shared<engine::Scene>();
                                     if (!scene->loadSceneFromFile(scenePath,
                                                                   [this](const std::string &status)
                                                                   { setLoadingStatus(status); }))
                                     {
                                         setLoadingStatus("Failed to load scene");
                                         VX_EDITOR_ERROR_STREAM("Failed to load scene asynchronously: " << scenePath << '\n');
                                         return nullptr;
                                     }
                                     setLoadingStatus("Scene is ready");
                                     return scene; });
}

void EditorRuntime::shutdownGameViewportRenderGraph()
{
    if (m_gameViewportRenderGraph)
        m_gameViewportRenderGraph->cleanResources();

    m_gameViewportRenderGraph.reset();
    m_gameViewportOutputHandlers = nullptr;
    m_lastGameRenderExtent = {};

    m_gameDepthPrepassRenderGraphPass = nullptr;
    m_gameGBufferRenderGraphPass = nullptr;
    m_gameShadowRenderGraphPass = nullptr;
    m_gameRTShadowsRenderGraphPass = nullptr;
    m_gameRTShadowDenoiseRenderGraphPass = nullptr;
    m_gameSSAORenderGraphPass = nullptr;
    m_gameLightingRenderGraphPass = nullptr;
    m_gameSkyLightRenderGraphPass = nullptr;
    m_gameParticleRenderGraphPass = nullptr;
    m_gameBloomRenderGraphPass = nullptr;
    m_gameAutoExposureRenderGraphPass = nullptr;
    m_gameTonemapRenderGraphPass = nullptr;
    m_gameBloomCompositeRenderGraphPass = nullptr;
    m_gameFXAARenderGraphPass = nullptr;
    m_gameSMAARenderGraphPass = nullptr;
    m_gameTAARenderGraphPass = nullptr;
    m_gameContactShadowRenderGraphPass = nullptr;
    m_gameSSRRenderGraphPass = nullptr;
    m_gameVolumetricFogLightingRenderGraphPass = nullptr;
    m_gameVolumetricFogTemporalRenderGraphPass = nullptr;
    m_gameVolumetricFogCompositeRenderGraphPass = nullptr;
    m_gameRTReflectionsRenderGraphPass = nullptr;
    m_gameRtaoRenderGraphPass = nullptr;
    m_gameRtaoDenoiseRenderGraphPass = nullptr;
    m_gameRtReflectionDenoiseRenderGraphPass = nullptr;
    m_gameRTGITemporalRenderGraphPass = nullptr;
    m_gameRTReflectionTemporalRenderGraphPass = nullptr;
    m_gameAutoExposureRenderGraphPass = nullptr;
    m_gameCinematicEffectsRenderGraphPass = nullptr;
    m_gameMotionBlurRenderGraphPass = nullptr;
    m_gameUIRenderGraphPass = nullptr;
    m_gameDecalRenderGraphPass = nullptr;
    m_gameViewportRenderGraphTopologyHash = 0u;
}

void EditorRuntime::initEditorRenderGraph()
{
    if (m_renderGraph)
        m_renderGraph->cleanResources();

    // Reset extent cache so applyEditorViewportExtent() always re-applies extents to
    // newly created pass objects (they initialize from swapchain extent in their ctors).
    m_lastEditorRenderExtent = {};
    m_lastGameRenderExtent = {};

    m_renderGraph = std::make_unique<engine::renderGraph::RenderGraph>(true);
    m_renderGraph->setCpuFrustumCullingEnabled(false);
    m_renderGraph->setCpuSmallFeatureCullingEnabled(false);

    m_depthPrepassRenderGraphPass = nullptr;
    m_gBufferRenderGraphPass = nullptr;
    m_shadowRenderGraphPass = nullptr;
    m_rtShadowsRenderGraphPass = nullptr;
    m_rtShadowDenoiseRenderGraphPass = nullptr;
    m_ssaoRenderGraphPass = nullptr;
    m_lightingRenderGraphPass = nullptr;
    m_contactShadowRenderGraphPass = nullptr;
    m_skyLightRenderGraphPass = nullptr;
    m_ssrRenderGraphPass = nullptr;
    m_volumetricFogLightingRenderGraphPass = nullptr;
    m_volumetricFogTemporalRenderGraphPass = nullptr;
    m_volumetricFogCompositeRenderGraphPass = nullptr;
    m_rtReflectionsRenderGraphPass = nullptr;
    m_rtaoRenderGraphPass = nullptr;
    m_rtaoDenoiseRenderGraphPass = nullptr;
    m_rtReflectionDenoiseRenderGraphPass = nullptr;
    m_rtReflectionTemporalRenderGraphPass = nullptr;
    m_rtGIRenderGraphPass = nullptr;
    m_rtGIDenoiseRenderGraphPass = nullptr;
    m_rtGITemporalRenderGraphPass = nullptr;
    m_particleRenderGraphPass = nullptr;
    m_sprite2DRenderGraphPass = nullptr;
    m_decalRenderGraphPass = nullptr;
    m_bloomRenderGraphPass = nullptr;
    m_autoExposureRenderGraphPass = nullptr;
    m_tonemapRenderGraphPass = nullptr;
    m_bloomCompositeRenderGraphPass = nullptr;
    m_fxaaRenderGraphPass = nullptr;
    m_smaaRenderGraphPass = nullptr;
    m_taaRenderGraphPass = nullptr;
    m_cinematicEffectsRenderGraphPass = nullptr;
    m_motionBlurRenderGraphPass = nullptr;
    m_objectIdResolveRenderGraphPass = nullptr;
    m_selectionOverlayRenderGraphPass = nullptr;
    m_debugOverlayRenderGraphPass = nullptr;
    m_uiRenderGraphPass = nullptr;
    m_editorBillboardRenderGraphPass = nullptr;
    m_imGuiRenderGraphPass = nullptr;
    m_previewAssetsRenderGraphPass = nullptr;
    m_animTreePreviewPass = nullptr;

    const auto context = core::VulkanContext::getContext();
    const auto &settings = engine::RenderQualitySettings::getInstance();
    const bool useSSAO = renderGraphUsesSSAO(settings, context);
    const bool useRTShadows = renderGraphUsesRTShadows(settings, context);
    const bool useRasterShadows = renderGraphUsesRasterShadows(settings, context, m_activeScene.get());
    const bool useRTAO = renderGraphUsesRTAO(settings, context);
    const bool useContactShadows = renderGraphUsesContactShadows(settings);
    const bool useRTReflections = renderGraphUsesRTReflections(settings, context);
    const bool useRTGI = renderGraphUsesRTGI(settings, context);
    const bool useSSR           = renderGraphUsesSSR(settings);
    const bool useVolumetricFog = renderGraphUsesVolumetricFog(settings, m_activeScene.get());
    const bool useBloom = renderGraphUsesBloom(settings);
    const auto aaMode = renderGraphAntiAliasingMode(settings);
    const bool useObjectIdResolve = context &&
        context->getEffectiveMsaaSampleCount(settings.getRequestedMsaaSampleCount()) != VK_SAMPLE_COUNT_1_BIT;
    const bool useCinematicEffects = renderGraphUsesCinematicEffects(settings);
    const bool useMotionBlur = renderGraphUsesMotionBlur(settings);
    const bool useUI = renderGraphUsesUI(m_activeScene.get());
    const bool useSelectionOverlay = editorRenderGraphUsesSelection(m_editor.get());
    m_gBufferRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::GBufferRenderGraphPass>(true);
    m_shadowRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::ShadowRenderGraphPass>();
    m_shadowRenderGraphPass->setSkipRendering(!useRasterShadows);

    if (useSSAO)
    {
        m_ssaoRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::SSAORenderGraphPass>(
            m_gBufferRenderGraphPass->getDepthTextureHandler(),
            m_gBufferRenderGraphPass->getNormalTextureHandlers());
    }

    if (useRTShadows)
    {
        m_rtShadowsRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::RTShadowsRenderGraphPass>(
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gBufferRenderGraphPass->getDepthTextureHandler());
        m_rtShadowDenoiseRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::RTShadowDenoiseRenderGraphPass>(
            m_rtShadowsRenderGraphPass->getOutput(),
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gBufferRenderGraphPass->getDepthTextureHandler());
    }

    if (useRTAO && m_ssaoRenderGraphPass)
    {
        m_rtaoRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::RTAORenderGraphPass>(
            m_gBufferRenderGraphPass->getDepthTextureHandler(),
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_ssaoRenderGraphPass->getAOHandlers());

        m_rtaoDenoiseRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::RTAODenoiseRenderGraphPass>(
            m_rtaoRenderGraphPass->getAOHandlers(),
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gBufferRenderGraphPass->getDepthTextureHandler());
    }

    m_decalRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::DecalRenderGraphPass>(
        m_gBufferRenderGraphPass->getAlbedoTextureHandlers(),
        m_gBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gBufferRenderGraphPass->getMaterialTextureHandlers(),
        m_gBufferRenderGraphPass->getEmissiveTextureHandlers(),
        m_gBufferRenderGraphPass->getDepthTextureHandler());
    m_decalRenderGraphPass->setScene(m_activeScene.get());

    auto *rtShadowHandlers = m_rtShadowDenoiseRenderGraphPass ? &m_rtShadowDenoiseRenderGraphPass->getOutput() : nullptr;
    auto *aoHandlers = m_rtaoDenoiseRenderGraphPass ? &m_rtaoDenoiseRenderGraphPass->getOutput()
                     : (m_rtaoRenderGraphPass       ? &m_rtaoRenderGraphPass->getAOHandlers()
                     : (m_ssaoRenderGraphPass        ? &m_ssaoRenderGraphPass->getAOHandlers() : nullptr));

    if (useRTGI)
    {
        m_rtGIRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::RTIndirectDiffuseRenderGraphPass>(
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gBufferRenderGraphPass->getAlbedoTextureHandlers(),
            m_gBufferRenderGraphPass->getDepthTextureHandler());

        if (settings.enableRTGIDenoiser)
        {
            m_rtGIDenoiseRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::RTGIDenoiseRenderGraphPass>(
                m_rtGIRenderGraphPass->getOutput(),
                m_gBufferRenderGraphPass->getNormalTextureHandlers(),
                m_gBufferRenderGraphPass->getDepthTextureHandler());
        }
    }
    if (m_rtGIDenoiseRenderGraphPass && settings.enableRTGI)
    {
        m_rtGITemporalRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::RTTemporalAccumulationRenderGraphPass>(
            m_rtGIDenoiseRenderGraphPass->getOutput(),
            m_gBufferRenderGraphPass->getDepthTextureHandler(),
            "RT GI Temporal Accumulation");
    }

    auto *giHandlers = m_rtGITemporalRenderGraphPass
                           ? &m_rtGITemporalRenderGraphPass->getOutput()
                           : (m_rtGIDenoiseRenderGraphPass
                                  ? &m_rtGIDenoiseRenderGraphPass->getOutput()
                                  : (m_rtGIRenderGraphPass ? &m_rtGIRenderGraphPass->getOutput() : nullptr));

    m_lightingRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::LightingRenderGraphPass>(
        m_shadowRenderGraphPass->getDirectionalShadowHandler(),
        m_gBufferRenderGraphPass->getDepthTextureHandler(),
        m_shadowRenderGraphPass->getCubeShadowHandler(),
        m_shadowRenderGraphPass->getSpotShadowHandler(),
        m_gBufferRenderGraphPass->getAlbedoTextureHandlers(),
        m_gBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gBufferRenderGraphPass->getMaterialTextureHandlers(),
        m_gBufferRenderGraphPass->getEmissiveTextureHandlers(),
        rtShadowHandlers,
        aoHandlers,
        giHandlers);

    auto *hdrSceneInput = &m_lightingRenderGraphPass->getOutput();
    if (useContactShadows)
    {
        m_contactShadowRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::ContactShadowRenderGraphPass>(
            *hdrSceneInput,
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gBufferRenderGraphPass->getDepthTextureHandler());
        hdrSceneInput = &m_contactShadowRenderGraphPass->getOutput();
    }

    m_skyLightRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::SkyLightRenderGraphPass>(
        *hdrSceneInput,
        m_gBufferRenderGraphPass->getDepthTextureHandler());

    hdrSceneInput = &m_skyLightRenderGraphPass->getOutput();
    if (useSSR)
    {
        m_ssrRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::SSRRenderGraphPass>(
            *hdrSceneInput,
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gBufferRenderGraphPass->getDepthTextureHandler(),
            m_gBufferRenderGraphPass->getMaterialTextureHandlers());
        hdrSceneInput = &m_ssrRenderGraphPass->getOutput();
    }

    if (useRTReflections)
    {
        m_rtReflectionsRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::RTReflectionsRenderGraphPass>(
            *hdrSceneInput,
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gBufferRenderGraphPass->getAlbedoTextureHandlers(),
            m_gBufferRenderGraphPass->getMaterialTextureHandlers(),
            m_gBufferRenderGraphPass->getDepthTextureHandler());
        m_rtReflectionDenoiseRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::RTReflectionDenoiseRenderGraphPass>(
            m_rtReflectionsRenderGraphPass->getOutput(),
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gBufferRenderGraphPass->getDepthTextureHandler());
        m_rtReflectionTemporalRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::RTTemporalAccumulationRenderGraphPass>(
            m_rtReflectionDenoiseRenderGraphPass->getOutput(),
            m_gBufferRenderGraphPass->getDepthTextureHandler(),
            "RT Reflection Temporal Accumulation");
        hdrSceneInput = &m_rtReflectionTemporalRenderGraphPass->getOutput();
    }

    if (useVolumetricFog)
    {
        m_volumetricFogLightingRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::VolumetricFogLightingRenderGraphPass>(
            m_gBufferRenderGraphPass->getDepthTextureHandler(),
            m_shadowRenderGraphPass->getDirectionalShadowHandler(),
            m_shadowRenderGraphPass->getCubeShadowHandler(),
            m_shadowRenderGraphPass->getSpotShadowHandler());

        auto *fogInput = &m_volumetricFogLightingRenderGraphPass->getOutput();
        if (settings.volumetricFogQuality == engine::RenderQualitySettings::VolumetricFogQuality::High)
        {
            m_volumetricFogTemporalRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::VolumetricFogTemporalRenderGraphPass>(
                *fogInput,
                m_gBufferRenderGraphPass->getDepthTextureHandler());
            fogInput = &m_volumetricFogTemporalRenderGraphPass->getOutput();
        }

        m_volumetricFogCompositeRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::VolumetricFogCompositeRenderGraphPass>(
            *hdrSceneInput,
            *fogInput);
        hdrSceneInput = &m_volumetricFogCompositeRenderGraphPass->getOutput();
    }

    m_particleRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::ParticleRenderGraphPass>(
        *hdrSceneInput,
        &m_gBufferRenderGraphPass->getDepthTextureHandler());
    m_particleRenderGraphPass->setScene(m_activeScene.get());
    hdrSceneInput = &m_particleRenderGraphPass->getHandlers();

    m_sprite2DRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::Sprite2DRenderGraphPass>(
        *hdrSceneInput);
    m_sprite2DRenderGraphPass->setScene(m_activeScene.get());
    hdrSceneInput = &m_sprite2DRenderGraphPass->getHandlers();

    if (useBloom)
    {
        m_bloomRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::BloomRenderGraphPass>(
            *hdrSceneInput);
    }

    if (settings.enableAutoExposure)
    {
        m_autoExposureRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::AutoExposureRenderGraphPass>(
            *hdrSceneInput);
    }

    m_tonemapRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::TonemapRenderGraphPass>(
        *hdrSceneInput);
    m_tonemapRenderGraphPass->setAutoExposurePass(m_autoExposureRenderGraphPass);

    auto *ldrSceneInput = &m_tonemapRenderGraphPass->getHandlers();
    if (useBloom && m_bloomRenderGraphPass)
    {
        m_bloomCompositeRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::BloomCompositeRenderGraphPass>(
            *ldrSceneInput,
            m_bloomRenderGraphPass->getHandlers());
        ldrSceneInput = &m_bloomCompositeRenderGraphPass->getHandlers();
    }

    switch (aaMode)
    {
    case engine::RenderQualitySettings::AntiAliasingMode::FXAA:
        m_fxaaRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::FXAARenderGraphPass>(*ldrSceneInput);
        ldrSceneInput = &m_fxaaRenderGraphPass->getHandlers();
        break;
    case engine::RenderQualitySettings::AntiAliasingMode::SMAA:
    case engine::RenderQualitySettings::AntiAliasingMode::CMAA:
        m_smaaRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::SMAAPassRenderGraphPass>(*ldrSceneInput);
        ldrSceneInput = &m_smaaRenderGraphPass->getHandlers();
        break;
    case engine::RenderQualitySettings::AntiAliasingMode::TAA:
        m_taaRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::TAARenderGraphPass>(*ldrSceneInput);
        ldrSceneInput = &m_taaRenderGraphPass->getHandlers();
        break;
    case engine::RenderQualitySettings::AntiAliasingMode::NONE:
    default:
        break;
    }

    if (useCinematicEffects)
    {
        m_cinematicEffectsRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::CinematicEffectsRenderGraphPass>(*ldrSceneInput);
        ldrSceneInput = &m_cinematicEffectsRenderGraphPass->getHandlers();
    }

    if (useMotionBlur)
    {
        m_motionBlurRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::MotionBlurRenderGraphPass>(
            *ldrSceneInput,
            m_gBufferRenderGraphPass->getDepthTextureHandler());
        ldrSceneInput = &m_motionBlurRenderGraphPass->getHandlers();
    }

    if (useObjectIdResolve)
    {
        m_objectIdResolveRenderGraphPass = m_renderGraph->addPass<ObjectIdResolveRenderGraphPass>(
            m_gBufferRenderGraphPass->getMsaaObjectTextureHandler(),
            m_gBufferRenderGraphPass->getObjectTextureHandler());
    }

    auto *editorSceneInput = ldrSceneInput;
    if (useSelectionOverlay)
    {
        m_selectionOverlayRenderGraphPass = m_renderGraph->addPass<SelectionOverlayRenderGraphPass>(
            m_editor,
            *editorSceneInput,
            m_gBufferRenderGraphPass->getObjectTextureHandler());
        editorSceneInput = &m_selectionOverlayRenderGraphPass->getHandlers();
    }

    m_debugOverlayRenderGraphPass = m_renderGraph->addPass<DebugOverlayRenderGraphPass>(
        *editorSceneInput);
    editorSceneInput = &m_debugOverlayRenderGraphPass->getHandlers();

    if (useUI)
    {
        m_uiRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::UIRenderGraphPass>(
            *editorSceneInput);
        editorSceneInput = &m_uiRenderGraphPass->getHandlers();
    }

    VkExtent2D previewExtent{.width = 256, .height = 256};
    m_previewAssetsRenderGraphPass = m_renderGraph->addPass<PreviewAssetsRenderGraphPass>(previewExtent);
    m_animTreePreviewPass = m_renderGraph->addPass<AnimationTreePreviewPass>();

    m_editorBillboardRenderGraphPass = m_renderGraph->addPass<EditorBillboardRenderGraphPass>(
        m_activeScene,
        *editorSceneInput);
    m_editorBillboardRenderGraphPass->setBillboardsVisible(engine::EngineConfig::instance().getShowEditorBillboards());
    editorSceneInput = &m_editorBillboardRenderGraphPass->getHandlers();

    m_imGuiRenderGraphPass = m_renderGraph->addPass<ImGuiRenderGraphPass>(
        m_editor,
        *editorSceneInput,
        m_gBufferRenderGraphPass->getObjectTextureHandler());

    m_renderGraph->setup();
    m_renderGraph->createRenderGraphResources();
    m_editorRenderGraphTopologyHash = editorRenderGraphTopologyHash(m_activeScene.get(), m_editor.get());

    // Eagerly register / re-register the anim-tree preview image with ImGui.
    // This must happen after createRenderGraphResources() (so compile() has run and
    // m_colorTarget is valid) but also needs to handle re-initialization when the render
    // graph is rebuilt due to a topology-hash change.
    if (m_animTreePreviewDescriptorSet != VK_NULL_HANDLE)
    {
        ImGui_ImplVulkan_RemoveTexture(m_animTreePreviewDescriptorSet);
        m_animTreePreviewDescriptorSet = VK_NULL_HANDLE;
    }
    if (m_animTreePreviewPass &&
        m_animTreePreviewPass->getOutputImageView() != VK_NULL_HANDLE &&
        m_animTreePreviewPass->getOutputSampler() != VK_NULL_HANDLE)
    {
        m_animTreePreviewDescriptorSet = ImGui_ImplVulkan_AddTexture(
            m_animTreePreviewPass->getOutputSampler(),
            m_animTreePreviewPass->getOutputImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    if (m_editor)
    {
        m_editor->setAnimTreePreviewPass(m_animTreePreviewPass);
        m_editor->setAnimTreePreviewDescriptorSet(m_animTreePreviewDescriptorSet);
    }
}

void EditorRuntime::initGameViewportRenderGraph()
{
    shutdownGameViewportRenderGraph();

    m_gameViewportRenderGraph = std::make_unique<engine::renderGraph::RenderGraph>(false);

    m_gameDepthPrepassRenderGraphPass = nullptr;
    m_gameGBufferRenderGraphPass = nullptr;
    m_gameShadowRenderGraphPass = nullptr;
    m_gameRTShadowsRenderGraphPass = nullptr;
    m_gameRTShadowDenoiseRenderGraphPass = nullptr;
    m_gameSSAORenderGraphPass = nullptr;
    m_gameLightingRenderGraphPass = nullptr;
    m_gameSkyLightRenderGraphPass = nullptr;
    m_gameParticleRenderGraphPass = nullptr;
    m_gameSprite2DRenderGraphPass = nullptr;
    m_gameBloomRenderGraphPass = nullptr;
    m_gameAutoExposureRenderGraphPass = nullptr;
    m_gameTonemapRenderGraphPass = nullptr;
    m_gameBloomCompositeRenderGraphPass = nullptr;
    m_gameFXAARenderGraphPass = nullptr;
    m_gameSMAARenderGraphPass = nullptr;
    m_gameTAARenderGraphPass = nullptr;
    m_gameContactShadowRenderGraphPass = nullptr;
    m_gameSSRRenderGraphPass = nullptr;
    m_gameRTReflectionsRenderGraphPass = nullptr;
    m_gameRtaoRenderGraphPass = nullptr;
    m_gameRtaoDenoiseRenderGraphPass = nullptr;
    m_gameRtReflectionDenoiseRenderGraphPass = nullptr;
    m_gameRTGIRenderGraphPass = nullptr;
    m_gameRTGIDenoiseRenderGraphPass = nullptr;
    m_gameRTGITemporalRenderGraphPass = nullptr;
    m_gameRTReflectionTemporalRenderGraphPass = nullptr;
    m_gameCinematicEffectsRenderGraphPass = nullptr;
    m_gameMotionBlurRenderGraphPass = nullptr;
    m_gameDebugOverlayRenderGraphPass = nullptr;
    m_gameUIRenderGraphPass = nullptr;
    m_gameViewportOutputHandlers = nullptr;

    const auto context = core::VulkanContext::getContext();
    const auto &settings = engine::RenderQualitySettings::getInstance();
    const bool useSSAO = renderGraphUsesSSAO(settings, context);
    const bool useRTShadows = renderGraphUsesRTShadows(settings, context);
    const bool useRasterShadows = renderGraphUsesRasterShadows(settings, context, m_activeScene.get());
    const bool useRTAO = renderGraphUsesRTAO(settings, context);
    const bool useContactShadows = renderGraphUsesContactShadows(settings);
    const bool useRTReflections = renderGraphUsesRTReflections(settings, context);
    const bool useRTGI = renderGraphUsesRTGI(settings, context);
    const bool useSSR           = renderGraphUsesSSR(settings);
    const bool useVolumetricFog = renderGraphUsesVolumetricFog(settings, m_activeScene.get());
    const bool useBloom = renderGraphUsesBloom(settings);
    const auto aaMode = renderGraphAntiAliasingMode(settings);
    const bool useCinematicEffects = renderGraphUsesCinematicEffects(settings);
    const bool useMotionBlur = renderGraphUsesMotionBlur(settings);
    const bool useUI = renderGraphUsesUI(m_activeScene.get());

    m_gameGBufferRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::GBufferRenderGraphPass>(false);
    m_gameShadowRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::ShadowRenderGraphPass>();
    m_gameShadowRenderGraphPass->setSkipRendering(!useRasterShadows);

    if (useSSAO)
    {
        m_gameSSAORenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::SSAORenderGraphPass>(
            m_gameGBufferRenderGraphPass->getDepthTextureHandler(),
            m_gameGBufferRenderGraphPass->getNormalTextureHandlers());
    }

    if (useRTShadows)
    {
        m_gameRTShadowsRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::RTShadowsRenderGraphPass>(
            m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gameGBufferRenderGraphPass->getDepthTextureHandler());
        m_gameRTShadowDenoiseRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::RTShadowDenoiseRenderGraphPass>(
            m_gameRTShadowsRenderGraphPass->getOutput(),
            m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gameGBufferRenderGraphPass->getDepthTextureHandler());
    }

    if (useRTAO && m_gameSSAORenderGraphPass)
    {
        m_gameRtaoRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::RTAORenderGraphPass>(
            m_gameGBufferRenderGraphPass->getDepthTextureHandler(),
            m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gameSSAORenderGraphPass->getAOHandlers());

        m_gameRtaoDenoiseRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::RTAODenoiseRenderGraphPass>(
            m_gameRtaoRenderGraphPass->getAOHandlers(),
            m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gameGBufferRenderGraphPass->getDepthTextureHandler());
    }

    m_gameDecalRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::DecalRenderGraphPass>(
        m_gameGBufferRenderGraphPass->getAlbedoTextureHandlers(),
        m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gameGBufferRenderGraphPass->getMaterialTextureHandlers(),
        m_gameGBufferRenderGraphPass->getEmissiveTextureHandlers(),
        m_gameGBufferRenderGraphPass->getDepthTextureHandler());
    m_gameDecalRenderGraphPass->setScene(m_activeScene.get());

    auto *gameRTShadowHandlers = m_gameRTShadowDenoiseRenderGraphPass ? &m_gameRTShadowDenoiseRenderGraphPass->getOutput() : nullptr;
    auto *gameAOHandlers = m_gameRtaoDenoiseRenderGraphPass ? &m_gameRtaoDenoiseRenderGraphPass->getOutput()
                         : (m_gameRtaoRenderGraphPass        ? &m_gameRtaoRenderGraphPass->getAOHandlers()
                         : (m_gameSSAORenderGraphPass         ? &m_gameSSAORenderGraphPass->getAOHandlers() : nullptr));

    if (useRTGI)
    {
        m_gameRTGIRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::RTIndirectDiffuseRenderGraphPass>(
            m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gameGBufferRenderGraphPass->getAlbedoTextureHandlers(),
            m_gameGBufferRenderGraphPass->getDepthTextureHandler());

        if (settings.enableRTGIDenoiser)
        {
            m_gameRTGIDenoiseRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::RTGIDenoiseRenderGraphPass>(
                m_gameRTGIRenderGraphPass->getOutput(),
                m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
                m_gameGBufferRenderGraphPass->getDepthTextureHandler());

            if (settings.enableRTGI)
            {
                m_gameRTGITemporalRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::RTTemporalAccumulationRenderGraphPass>(
                    m_gameRTGIDenoiseRenderGraphPass->getOutput(),
                    m_gameGBufferRenderGraphPass->getDepthTextureHandler(),
                    "RT GI Temporal Accumulation (Game)");
            }
        }
    }
    auto *gameGIHandlers = m_gameRTGITemporalRenderGraphPass
                               ? &m_gameRTGITemporalRenderGraphPass->getOutput()
                               : (m_gameRTGIDenoiseRenderGraphPass
                                      ? &m_gameRTGIDenoiseRenderGraphPass->getOutput()
                                      : (m_gameRTGIRenderGraphPass ? &m_gameRTGIRenderGraphPass->getOutput() : nullptr));

    m_gameLightingRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::LightingRenderGraphPass>(
        m_gameShadowRenderGraphPass->getDirectionalShadowHandler(),
        m_gameGBufferRenderGraphPass->getDepthTextureHandler(),
        m_gameShadowRenderGraphPass->getCubeShadowHandler(),
        m_gameShadowRenderGraphPass->getSpotShadowHandler(),
        m_gameGBufferRenderGraphPass->getAlbedoTextureHandlers(),
        m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gameGBufferRenderGraphPass->getMaterialTextureHandlers(),
        m_gameGBufferRenderGraphPass->getEmissiveTextureHandlers(),
        gameRTShadowHandlers,
        gameAOHandlers,
        gameGIHandlers);

    auto *hdrSceneInput = &m_gameLightingRenderGraphPass->getOutput();
    if (useContactShadows)
    {
        m_gameContactShadowRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::ContactShadowRenderGraphPass>(
            *hdrSceneInput,
            m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gameGBufferRenderGraphPass->getDepthTextureHandler());
        hdrSceneInput = &m_gameContactShadowRenderGraphPass->getOutput();
    }

    m_gameSkyLightRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::SkyLightRenderGraphPass>(
        *hdrSceneInput,
        m_gameGBufferRenderGraphPass->getDepthTextureHandler());

    hdrSceneInput = &m_gameSkyLightRenderGraphPass->getOutput();
    if (useSSR)
    {
        m_gameSSRRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::SSRRenderGraphPass>(
            *hdrSceneInput,
            m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gameGBufferRenderGraphPass->getDepthTextureHandler(),
            m_gameGBufferRenderGraphPass->getMaterialTextureHandlers());
        hdrSceneInput = &m_gameSSRRenderGraphPass->getOutput();
    }

    if (useRTReflections)
    {
        m_gameRTReflectionsRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::RTReflectionsRenderGraphPass>(
            *hdrSceneInput,
            m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gameGBufferRenderGraphPass->getAlbedoTextureHandlers(),
            m_gameGBufferRenderGraphPass->getMaterialTextureHandlers(),
            m_gameGBufferRenderGraphPass->getDepthTextureHandler());
        m_gameRtReflectionDenoiseRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::RTReflectionDenoiseRenderGraphPass>(
            m_gameRTReflectionsRenderGraphPass->getOutput(),
            m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gameGBufferRenderGraphPass->getDepthTextureHandler());
        m_gameRTReflectionTemporalRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::RTTemporalAccumulationRenderGraphPass>(
            m_gameRtReflectionDenoiseRenderGraphPass->getOutput(),
            m_gameGBufferRenderGraphPass->getDepthTextureHandler(),
            "RT Reflection Temporal Accumulation (Game)");
        hdrSceneInput = &m_gameRTReflectionTemporalRenderGraphPass->getOutput();
    }

    if (useVolumetricFog)
    {
        m_gameVolumetricFogLightingRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::VolumetricFogLightingRenderGraphPass>(
            m_gameGBufferRenderGraphPass->getDepthTextureHandler(),
            m_gameShadowRenderGraphPass->getDirectionalShadowHandler(),
            m_gameShadowRenderGraphPass->getCubeShadowHandler(),
            m_gameShadowRenderGraphPass->getSpotShadowHandler());

        auto *fogInput = &m_gameVolumetricFogLightingRenderGraphPass->getOutput();
        if (settings.volumetricFogQuality == engine::RenderQualitySettings::VolumetricFogQuality::High)
        {
            m_gameVolumetricFogTemporalRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::VolumetricFogTemporalRenderGraphPass>(
                *fogInput,
                m_gameGBufferRenderGraphPass->getDepthTextureHandler());
            fogInput = &m_gameVolumetricFogTemporalRenderGraphPass->getOutput();
        }

        m_gameVolumetricFogCompositeRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::VolumetricFogCompositeRenderGraphPass>(
            *hdrSceneInput,
            *fogInput);
        hdrSceneInput = &m_gameVolumetricFogCompositeRenderGraphPass->getOutput();
    }

    m_gameParticleRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::ParticleRenderGraphPass>(
        *hdrSceneInput,
        &m_gameGBufferRenderGraphPass->getDepthTextureHandler());
    m_gameParticleRenderGraphPass->setScene(m_activeScene.get());
    hdrSceneInput = &m_gameParticleRenderGraphPass->getHandlers();

    m_gameSprite2DRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::Sprite2DRenderGraphPass>(
        *hdrSceneInput);
    m_gameSprite2DRenderGraphPass->setScene(m_activeScene.get());
    hdrSceneInput = &m_gameSprite2DRenderGraphPass->getHandlers();

    if (useBloom)
    {
        m_gameBloomRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::BloomRenderGraphPass>(
            *hdrSceneInput);
    }

    if (settings.enableAutoExposure)
    {
        m_gameAutoExposureRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::AutoExposureRenderGraphPass>(
            *hdrSceneInput);
    }

    m_gameTonemapRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::TonemapRenderGraphPass>(
        *hdrSceneInput);
    m_gameTonemapRenderGraphPass->setAutoExposurePass(m_gameAutoExposureRenderGraphPass);

    auto *ldrSceneInput = &m_gameTonemapRenderGraphPass->getHandlers();
    if (useBloom && m_gameBloomRenderGraphPass)
    {
        m_gameBloomCompositeRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::BloomCompositeRenderGraphPass>(
            *ldrSceneInput,
            m_gameBloomRenderGraphPass->getHandlers());
        ldrSceneInput = &m_gameBloomCompositeRenderGraphPass->getHandlers();
    }

    switch (aaMode)
    {
    case engine::RenderQualitySettings::AntiAliasingMode::FXAA:
        m_gameFXAARenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::FXAARenderGraphPass>(*ldrSceneInput);
        ldrSceneInput = &m_gameFXAARenderGraphPass->getHandlers();
        break;
    case engine::RenderQualitySettings::AntiAliasingMode::SMAA:
    case engine::RenderQualitySettings::AntiAliasingMode::CMAA:
        m_gameSMAARenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::SMAAPassRenderGraphPass>(*ldrSceneInput);
        ldrSceneInput = &m_gameSMAARenderGraphPass->getHandlers();
        break;
    case engine::RenderQualitySettings::AntiAliasingMode::TAA:
        m_gameTAARenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::TAARenderGraphPass>(*ldrSceneInput);
        ldrSceneInput = &m_gameTAARenderGraphPass->getHandlers();
        break;
    case engine::RenderQualitySettings::AntiAliasingMode::NONE:
    default:
        break;
    }

    if (useCinematicEffects)
    {
        m_gameCinematicEffectsRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::CinematicEffectsRenderGraphPass>(*ldrSceneInput);
        ldrSceneInput = &m_gameCinematicEffectsRenderGraphPass->getHandlers();
    }

    if (useMotionBlur)
    {
        m_gameMotionBlurRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::MotionBlurRenderGraphPass>(
            *ldrSceneInput,
            m_gameGBufferRenderGraphPass->getDepthTextureHandler());
        ldrSceneInput = &m_gameMotionBlurRenderGraphPass->getHandlers();
    }

    m_gameDebugOverlayRenderGraphPass = m_gameViewportRenderGraph->addPass<DebugOverlayRenderGraphPass>(
        *ldrSceneInput);
    ldrSceneInput = &m_gameDebugOverlayRenderGraphPass->getHandlers();

    if (useUI)
    {
        m_gameUIRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::UIRenderGraphPass>(
            *ldrSceneInput);
        ldrSceneInput = &m_gameUIRenderGraphPass->getHandlers();
    }
    m_gameViewportOutputHandlers = ldrSceneInput;

    m_gameViewportRenderGraph->setup();
    m_gameViewportRenderGraph->createRenderGraphResources();
    m_gameViewportRenderGraphTopologyHash = gameViewportRenderGraphTopologyHash(m_activeScene.get());
}

void EditorRuntime::addLoadingRenderToImGui()
{
    m_editor->queueViewportDraw("Viewport",
                                [this](ImDrawList *dl, const ImVec2 &origin, const ImVec2 &size)
                                {
                                    ImVec2 center = ImVec2(
                                        origin.x + size.x * 0.5f,
                                        origin.y + size.y * 0.5f);

                                    auto ds = m_editor->getEditorResourceStorage()
                                                  .getTextureDescriptorSet("./resources/textures/VelixV.tex.elixasset");

                                    ImTextureID texId = (ImTextureID)(uintptr_t)ds;

                                    float logoSize = 256.0f;
                                    float spacing = 20.0f;

                                    ImVec2 logoMin = ImVec2(
                                        center.x - logoSize * 0.5f,
                                        center.y - logoSize * 0.5f - 30.0f);

                                    ImVec2 logoMax = ImVec2(
                                        logoMin.x + logoSize,
                                        logoMin.y + logoSize);

                                    dl->AddImage(
                                        texId,
                                        logoMin,
                                        logoMax,
                                        ImVec2(0, 0),
                                        ImVec2(1, 1),
                                        IM_COL32(255, 255, 255, 255));

                                    const float elapsedSeconds = std::chrono::duration<float>(
                                                                     std::chrono::steady_clock::now() - m_loadingStartedAt)
                                                                     .count();
                                    const int dotCount = static_cast<int>(std::fmod(std::max(elapsedSeconds, 0.0f) * 2.0f, 4.0f));
                                    std::string loadingText = "Loading";
                                    loadingText.append(static_cast<size_t>(dotCount), '.');

                                    if (!m_loadingSceneName.empty())
                                        loadingText += " " + m_loadingSceneName;

                                    const std::string statusText = getLoadingStatus();

                                    ImVec2 textSize = ImGui::CalcTextSize(loadingText.c_str());

                                    ImVec2 textPos = ImVec2(
                                        center.x - textSize.x * 0.5f,
                                        logoMax.y + spacing);

                                    dl->AddText(
                                        ImGui::GetFont(),
                                        ImGui::GetFontSize(),
                                        textPos,
                                        IM_COL32(255, 255, 255, 255),
                                        loadingText.c_str());

                                    const std::string elapsedText = "Elapsed: " + std::to_string(elapsedSeconds).substr(0, 4) + "s";
                                    const ImVec2 statusSize = ImGui::CalcTextSize(statusText.c_str());
                                    const ImVec2 elapsedSize = ImGui::CalcTextSize(elapsedText.c_str());

                                    const ImVec2 statusPos(
                                        center.x - statusSize.x * 0.5f,
                                        textPos.y + ImGui::GetFontSize() + 8.0f);
                                    const ImVec2 elapsedPos(
                                        center.x - elapsedSize.x * 0.5f,
                                        statusPos.y + ImGui::GetFontSize() + 6.0f);

                                    dl->AddText(
                                        ImGui::GetFont(),
                                        ImGui::GetFontSize(),
                                        statusPos,
                                        IM_COL32(210, 210, 210, 255),
                                        statusText.c_str());

                                    dl->AddText(
                                        ImGui::GetFont(),
                                        ImGui::GetFontSize(),
                                        elapsedPos,
                                        IM_COL32(170, 170, 170, 255),
                                        elapsedText.c_str());
                                });
}

void EditorRuntime::tick(float deltaTime)
{
    if (!m_activeScene || !m_editor || !m_renderGraph)
        return;

    engine::ReflectionProbeComponent::flushDeferredCapturedCubemapReleases();

    const bool shaderReloadRequested = m_editor->consumeShaderReloadRequest();
    if (shaderReloadRequested)
        engine::GraphicsPipelineManager::reloadShaders();

    if (m_stillLoadingTheScene && m_loadingFuture.valid())
    {
        const auto state = m_loadingFuture.wait_for(std::chrono::milliseconds(0));
        if (state == std::future_status::ready)
        {
            auto loadedScene = m_loadingFuture.get();
            if (loadedScene)
            {
                m_editorScene = loadedScene;
                m_currentEditorScenePath = m_pendingEditorScenePath;
                switchActiveScene(m_editorScene);
                m_editor->setCurrentScenePath(m_currentEditorScenePath);
                VX_EDITOR_INFO_STREAM("Scene loading completed: " << m_currentEditorScenePath.string() << '\n');
            }
            else
            {
                VX_EDITOR_ERROR_STREAM("Scene loading failed, keeping empty editor scene.\n");
            }

            m_stillLoadingTheScene = false;
        }
    }

    if (m_shouldUpdate)
        m_activeScene->update(deltaTime);
    else
    {
        m_editor->updateAnimationPreview(deltaTime);

        if (!m_isPlaySessionActive)
        {
            for (const auto &entity : m_activeScene->getEntities())
            {
                if (!entity || !entity->isEnabled())
                    continue;

                for (auto *particleSystemComponent : entity->getComponents<engine::ParticleSystemComponent>())
                {
                    if (!particleSystemComponent)
                        continue;

                    particleSystemComponent->update(deltaTime);
                }
            }
        }
    }

    m_previewAssetsRenderGraphPass->clearJobs();
    for (const auto &previewJob : m_editor->getRequestedPreviewJobs())
    {
        PreviewAssetsRenderGraphPass::PreviewJob job;
        job.material = previewJob.material;
        job.mesh = previewJob.mesh;
        job.modelTransform = previewJob.modelTransform;
        m_previewAssetsRenderGraphPass->addPreviewJob(job);
    }

    const uint32_t editorViewportWidth = m_editor->getViewportX();
    const uint32_t editorViewportHeight = m_editor->getViewportY();
    applyEditorViewportExtent(editorViewportWidth, editorViewportHeight);
    if (m_editorRenderCamera && editorViewportWidth > 0 && editorViewportHeight > 0)
        m_editorRenderCamera->setAspect(static_cast<float>(editorViewportWidth) / static_cast<float>(editorViewportHeight));

    m_gameRenderCamera = nullptr;
    for (const auto &entity : m_activeScene->getEntities())
    {
        if (!entity || !entity->isEnabled())
            continue;

        if (auto *cameraComponent = entity->getComponent<engine::CameraComponent>())
        {
            m_gameRenderCamera = cameraComponent->getCamera();
            break;
        }
    }

    const bool shouldRenderGameViewport = m_isPlaySessionActive || m_editor->isGameViewportVisible();
    if (!shouldRenderGameViewport && m_gameViewportRenderGraph)
        shutdownGameViewportRenderGraph();
    if (shouldRenderGameViewport && !m_gameViewportRenderGraph)
        initGameViewportRenderGraph();

    const uint32_t gameViewportWidth = m_editor->getGameViewportX();
    const uint32_t gameViewportHeight = m_editor->getGameViewportY();
    applyGameViewportExtent(gameViewportWidth, gameViewportHeight);
    if (m_gameRenderCamera && gameViewportWidth > 0 && gameViewportHeight > 0)
        m_gameRenderCamera->setAspect(static_cast<float>(gameViewportWidth) / static_cast<float>(gameViewportHeight));

    engine::DebugDraw::flush(deltaTime);

    const size_t currentEditorTopologyHash = editorRenderGraphTopologyHash(m_activeScene.get(), m_editor.get());
    if (currentEditorTopologyHash != m_editorRenderGraphTopologyHash)
    {
        VX_EDITOR_INFO_STREAM("Editor render graph topology changed: "
                              << m_editorRenderGraphTopologyHash
                              << " -> "
                              << currentEditorTopologyHash
                              << '\n');
        initEditorRenderGraph();
        applyEditorViewportExtent(editorViewportWidth, editorViewportHeight);
    }

    const size_t currentGameViewportTopologyHash = gameViewportRenderGraphTopologyHash(m_activeScene.get());
    if (shouldRenderGameViewport && currentGameViewportTopologyHash != m_gameViewportRenderGraphTopologyHash)
    {
        VX_EDITOR_INFO_STREAM("Game viewport render graph topology changed: "
                              << m_gameViewportRenderGraphTopologyHash
                              << " -> "
                              << currentGameViewportTopologyHash
                              << '\n');
        initGameViewportRenderGraph();
        applyGameViewportExtent(gameViewportWidth, gameViewportHeight);
    }

    engine::ui::UIRenderData uiData;
    for (const auto &text : m_activeScene->getUITexts())
        if (text && text->isEnabled())
            uiData.texts.push_back(text.get());
    for (const auto &button : m_activeScene->getUIButtons())
        if (button && button->isEnabled())
            uiData.buttons.push_back(button.get());
    for (const auto &billboard : m_activeScene->getBillboards())
        if (billboard && billboard->isEnabled())
            uiData.billboards.push_back(billboard.get());

    if (m_uiRenderGraphPass)
        m_uiRenderGraphPass->setRenderData(uiData);
    if (m_gameUIRenderGraphPass)
        m_gameUIRenderGraphPass->setRenderData(uiData);
    if (m_editorBillboardRenderGraphPass)
        m_editorBillboardRenderGraphPass->setBillboardsVisible(engine::EngineConfig::instance().getShowEditorBillboards());

    if (m_shadowRenderGraphPass)
        m_shadowRenderGraphPass->syncQualitySettings();
    if (m_gameShadowRenderGraphPass)
        m_gameShadowRenderGraphPass->syncQualitySettings();

    if (m_gameViewportRenderGraph && m_gameRenderCamera && shouldRenderGameViewport)
    {
        syncNearestReflectionProbe(m_gameLightingRenderGraphPass, m_activeScene.get(), m_gameRenderCamera);
        m_gameViewportRenderGraph->prepareFrame(m_gameRenderCamera, m_activeScene.get(), deltaTime);
        m_gameViewportRenderGraph->draw();

        if (m_imGuiRenderGraphPass && m_gameViewportOutputHandlers)
            m_imGuiRenderGraphPass->setGameViewportImages(
                m_gameViewportRenderGraph->getImageViews(*m_gameViewportOutputHandlers),
                true,
                m_gameViewportRenderGraph->getCurrentImageIndex());
    }
    else if (m_imGuiRenderGraphPass)
    {
        m_imGuiRenderGraphPass->setGameViewportImages({}, m_gameRenderCamera != nullptr, 0u);
    }

    if (m_stillLoadingTheScene)
        addLoadingRenderToImGui();

    // --- Reflection probe: capture first, then scan ---
    // Capture runs before the probe scan so the scan picks up the freshly captured view.
    // captureSceneProbe reuses the previous frame's drawBatches (still valid at this point).
    if (m_pendingProbeCaptureEntity)
    {
        captureReflectionProbe(m_pendingProbeCaptureEntity);
        m_pendingProbeCaptureEntity = nullptr;
    }

    syncNearestReflectionProbe(m_lightingRenderGraphPass, m_activeScene.get(), m_editorRenderCamera);
    syncNearestReflectionProbe(m_gameLightingRenderGraphPass, m_activeScene.get(), m_gameRenderCamera);

    m_renderGraph->prepareFrame(m_editorRenderCamera, m_activeScene.get(), deltaTime);

    m_editor->setRenderGraphProfilingData(m_renderGraph->getLastFrameBenchmarkData());
    m_renderGraph->draw();
    m_editor->processPendingObjectSelection();

    m_editor->setDonePreviewJobs(m_previewAssetsRenderGraphPass->getRenderedImages());
}

void EditorRuntime::captureReflectionProbe(engine::Entity *entity)
{
    if (!entity || !m_renderGraph)
        return;

    auto *probe     = entity->getComponent<engine::ReflectionProbeComponent>();
    auto *transform = entity->getComponent<engine::Transform3DComponent>();
    if (!probe || !transform)
        return;

    const glm::vec3 probePos = transform->getWorldPosition();
    auto result = m_renderGraph->captureSceneProbe(probePos, 256, m_activeScene.get());
    if (!result.success())
    {
        VX_ENGINE_WARNING_STREAM("Reflection probe capture failed for entity '" << entity->getName() << "'");
        return;
    }

    probe->setCapturedCubemap(std::move(result.image), result.cubeImageView, std::move(result.sampler));
    VX_ENGINE_INFO_STREAM("Reflection probe captured for entity '" << entity->getName() << "'");
}

void EditorRuntime::setAnimTreePreviewData(const AnimPreviewDrawData &data)
{
    if (m_animTreePreviewPass)
        m_animTreePreviewPass->setPreviewData(data);
}

void EditorRuntime::applyEditorViewportExtent(uint32_t width, uint32_t height)
{
    if (width == 0u || height == 0u)
        return;

    const VkExtent2D extent = makeScaledRenderExtent(width, height);
    if (extent.width == m_lastEditorRenderExtent.width && extent.height == m_lastEditorRenderExtent.height)
        return;

    VX_EDITOR_INFO_STREAM("Editor viewport extent changed: panel="
                          << width << 'x' << height
                          << ", scaled="
                          << extent.width << 'x' << extent.height
                          << '\n');

    if (m_gBufferRenderGraphPass)
        m_gBufferRenderGraphPass->setExtent(extent);
    if (m_rtShadowsRenderGraphPass)
        m_rtShadowsRenderGraphPass->setExtent(extent);
    if (m_rtShadowDenoiseRenderGraphPass)
        m_rtShadowDenoiseRenderGraphPass->setExtent(extent);
    if (m_ssaoRenderGraphPass)
        m_ssaoRenderGraphPass->setExtent(extent);
    if (m_lightingRenderGraphPass)
        m_lightingRenderGraphPass->setExtent(extent);
    if (m_ssrRenderGraphPass)
        m_ssrRenderGraphPass->setExtent(extent);
    if (m_volumetricFogLightingRenderGraphPass)
        m_volumetricFogLightingRenderGraphPass->setExtent(extent);
    if (m_volumetricFogTemporalRenderGraphPass)
        m_volumetricFogTemporalRenderGraphPass->setExtent(extent);
    if (m_volumetricFogCompositeRenderGraphPass)
        m_volumetricFogCompositeRenderGraphPass->setExtent(extent);
    if (m_rtReflectionsRenderGraphPass)
        m_rtReflectionsRenderGraphPass->setExtent(extent);
    if (m_rtaoRenderGraphPass)
        m_rtaoRenderGraphPass->setExtent(extent);
    if (m_rtaoDenoiseRenderGraphPass)
        m_rtaoDenoiseRenderGraphPass->setExtent(extent);
    if (m_rtReflectionDenoiseRenderGraphPass)
        m_rtReflectionDenoiseRenderGraphPass->setExtent(extent);
    if (m_rtReflectionTemporalRenderGraphPass)
        m_rtReflectionTemporalRenderGraphPass->setExtent(extent);
    if (m_rtGIRenderGraphPass)
        m_rtGIRenderGraphPass->setExtent(extent);
    if (m_rtGIDenoiseRenderGraphPass)
        m_rtGIDenoiseRenderGraphPass->setExtent(extent);
    if (m_rtGITemporalRenderGraphPass)
        m_rtGITemporalRenderGraphPass->setExtent(extent);
    if (m_skyLightRenderGraphPass)
        m_skyLightRenderGraphPass->setExtent(extent);
    if (m_bloomRenderGraphPass)
        m_bloomRenderGraphPass->setExtent(extent);
    if (m_autoExposureRenderGraphPass)
        m_autoExposureRenderGraphPass->setExtent(extent);
    if (m_tonemapRenderGraphPass)
        m_tonemapRenderGraphPass->setExtent(extent);
    if (m_bloomCompositeRenderGraphPass)
        m_bloomCompositeRenderGraphPass->setExtent(extent);
    if (m_fxaaRenderGraphPass)
        m_fxaaRenderGraphPass->setExtent(extent);
    if (m_smaaRenderGraphPass)
        m_smaaRenderGraphPass->setExtent(extent);
    if (m_taaRenderGraphPass)
        m_taaRenderGraphPass->setExtent(extent);
    if (m_contactShadowRenderGraphPass)
        m_contactShadowRenderGraphPass->setExtent(extent);
    if (m_cinematicEffectsRenderGraphPass)
        m_cinematicEffectsRenderGraphPass->setExtent(extent);
    if (m_motionBlurRenderGraphPass)
        m_motionBlurRenderGraphPass->setExtent(extent);
    if (m_objectIdResolveRenderGraphPass)
        m_objectIdResolveRenderGraphPass->setExtent(extent);
    if (m_selectionOverlayRenderGraphPass)
        m_selectionOverlayRenderGraphPass->setExtent(extent);
    if (m_debugOverlayRenderGraphPass)
        m_debugOverlayRenderGraphPass->setExtent(extent);
    if (m_uiRenderGraphPass)
        m_uiRenderGraphPass->setExtent(extent);
    if (m_editorBillboardRenderGraphPass)
        m_editorBillboardRenderGraphPass->setExtent(extent);
    if (m_particleRenderGraphPass)
        m_particleRenderGraphPass->setExtent(extent);
    if (m_sprite2DRenderGraphPass)
        m_sprite2DRenderGraphPass->setExtent(extent);

    m_lastEditorRenderExtent = extent;
}

void EditorRuntime::applyGameViewportExtent(uint32_t width, uint32_t height)
{
    if (!m_gameViewportRenderGraph || !m_gameGBufferRenderGraphPass)
        return;
    if (width == 0u || height == 0u)
        return;

    const VkExtent2D extent = makeScaledRenderExtent(width, height);
    if (extent.width == m_lastGameRenderExtent.width && extent.height == m_lastGameRenderExtent.height)
        return;

    VX_EDITOR_INFO_STREAM("Game viewport extent changed: panel="
                          << width << 'x' << height
                          << ", scaled="
                          << extent.width << 'x' << extent.height
                          << '\n');

    if (m_gameGBufferRenderGraphPass)
        m_gameGBufferRenderGraphPass->setExtent(extent);
    if (m_gameRTShadowsRenderGraphPass)
        m_gameRTShadowsRenderGraphPass->setExtent(extent);
    if (m_gameRTShadowDenoiseRenderGraphPass)
        m_gameRTShadowDenoiseRenderGraphPass->setExtent(extent);
    if (m_gameSSAORenderGraphPass)
        m_gameSSAORenderGraphPass->setExtent(extent);
    if (m_gameLightingRenderGraphPass)
        m_gameLightingRenderGraphPass->setExtent(extent);
    if (m_gameSSRRenderGraphPass)
        m_gameSSRRenderGraphPass->setExtent(extent);
    if (m_gameVolumetricFogLightingRenderGraphPass)
        m_gameVolumetricFogLightingRenderGraphPass->setExtent(extent);
    if (m_gameVolumetricFogTemporalRenderGraphPass)
        m_gameVolumetricFogTemporalRenderGraphPass->setExtent(extent);
    if (m_gameVolumetricFogCompositeRenderGraphPass)
        m_gameVolumetricFogCompositeRenderGraphPass->setExtent(extent);
    if (m_gameRTReflectionsRenderGraphPass)
        m_gameRTReflectionsRenderGraphPass->setExtent(extent);
    if (m_gameRtaoRenderGraphPass)
        m_gameRtaoRenderGraphPass->setExtent(extent);
    if (m_gameRtaoDenoiseRenderGraphPass)
        m_gameRtaoDenoiseRenderGraphPass->setExtent(extent);
    if (m_gameRtReflectionDenoiseRenderGraphPass)
        m_gameRtReflectionDenoiseRenderGraphPass->setExtent(extent);
    if (m_gameRTGIRenderGraphPass)
        m_gameRTGIRenderGraphPass->setExtent(extent);
    if (m_gameRTGIDenoiseRenderGraphPass)
        m_gameRTGIDenoiseRenderGraphPass->setExtent(extent);
    if (m_gameRTGITemporalRenderGraphPass)
        m_gameRTGITemporalRenderGraphPass->setExtent(extent);
    if (m_gameRTReflectionTemporalRenderGraphPass)
        m_gameRTReflectionTemporalRenderGraphPass->setExtent(extent);
    if (m_gameSkyLightRenderGraphPass)
        m_gameSkyLightRenderGraphPass->setExtent(extent);
    if (m_gameBloomRenderGraphPass)
        m_gameBloomRenderGraphPass->setExtent(extent);
    if (m_gameTonemapRenderGraphPass)
        m_gameTonemapRenderGraphPass->setExtent(extent);
    if (m_gameBloomCompositeRenderGraphPass)
        m_gameBloomCompositeRenderGraphPass->setExtent(extent);
    if (m_gameFXAARenderGraphPass)
        m_gameFXAARenderGraphPass->setExtent(extent);
    if (m_gameSMAARenderGraphPass)
        m_gameSMAARenderGraphPass->setExtent(extent);
    if (m_gameTAARenderGraphPass)
        m_gameTAARenderGraphPass->setExtent(extent);
    if (m_gameContactShadowRenderGraphPass)
        m_gameContactShadowRenderGraphPass->setExtent(extent);
    if (m_gameCinematicEffectsRenderGraphPass)
        m_gameCinematicEffectsRenderGraphPass->setExtent(extent);
    if (m_gameMotionBlurRenderGraphPass)
        m_gameMotionBlurRenderGraphPass->setExtent(extent);
    if (m_gameDebugOverlayRenderGraphPass)
        m_gameDebugOverlayRenderGraphPass->setExtent(extent);
    if (m_gameUIRenderGraphPass)
        m_gameUIRenderGraphPass->setExtent(extent);
    if (m_gameParticleRenderGraphPass)
        m_gameParticleRenderGraphPass->setExtent(extent);
    if (m_gameAutoExposureRenderGraphPass)
        m_gameAutoExposureRenderGraphPass->setExtent(extent);
    if (m_gameSprite2DRenderGraphPass)
        m_gameSprite2DRenderGraphPass->setExtent(extent);

    m_lastGameRenderExtent = extent;
}

void EditorRuntime::shutdown()
{
    if (m_loadingFuture.valid())
        m_loadingFuture.wait();

    engine::ScriptsRegister::setActiveRegister(nullptr);

    releaseScriptInstancesForScenes({m_activeScene, m_playScene, m_editorScene});

    if (m_editor)
    {
        m_editor->setProjectScriptsRegister(nullptr, {});
        m_editor->setScene(nullptr);
    }

    m_activeScene.reset();
    m_playScene.reset();
    m_editorScene.reset();

    if (m_renderGraph)
        m_renderGraph->cleanResources();

    shutdownGameViewportRenderGraph();

    ImGuiRenderGraphPass::shutdownPersistentImGuiBackend();

    engine::scripting::setActiveScene(nullptr);
    engine::scripting::setActiveWindow(nullptr);

    engine::PluginManager::instance().unloadAll();

    if (m_editor)
        m_editor->saveProjectConfig();

    if (m_project && m_project->projectLibrary)
        m_project->unloadProjectLibrary();

    if (m_project)
        m_project->clearCache();

    engine::AssetsLoader::setTextureAssetImportRootDirectory({});
}

ELIX_NESTED_NAMESPACE_END
