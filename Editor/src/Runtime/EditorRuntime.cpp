#include "Editor/Runtime/EditorRuntime.hpp"

#include "Editor/Editor.hpp"
#include "Editor/ImGuiRenderGraphPass.hpp"
#include "Editor/Project.hpp"
#include "Editor/ProjectLoader.hpp"
#include "Editor/EditorResourcesStorage.hpp"

#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Components/CameraComponent.hpp"
#include "Engine/Components/ParticleSystemComponent.hpp"
#include "Engine/Components/ScriptComponent.hpp"
#include "Engine/PluginSystem/PluginLoader.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Scripting/ScriptsRegister.hpp"
#include "Engine/Scripting/VelixAPI.hpp"

#include "Core/VulkanContext.hpp"

#include "Core/Logger.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
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

    size_t renderGraphTopologyHash()
    {
        const auto &settings = elix::engine::RenderQualitySettings::getInstance();
        const auto context = elix::core::VulkanContext::getContext();
        const bool supportsRayQuery = context && context->hasRayQuerySupport();
        const bool supportsRayPipeline = context && context->hasRayTracingPipelineSupport();
        const bool supportsAnyRT = supportsRayQuery || supportsRayPipeline;

        size_t seed = 0u;
        hashCombine(seed, settings.enablePostProcessing);
        hashCombine(seed, settings.enableRayTracing && settings.enableRTShadows && supportsAnyRT);
        hashCombine(seed, settings.enableRayTracing && settings.enableRTReflections && supportsAnyRT);
        hashCombine(seed, settings.enableRayTracing && settings.enableRTAO && supportsRayQuery);
        return seed;
    }

    std::filesystem::path findGameModuleLibraryPath(const std::filesystem::path &buildDirectory)
    {
        if (buildDirectory.empty() || !std::filesystem::exists(buildDirectory))
            return {};

        const std::string libraryExtension = sharedLibraryExtension();
        if (libraryExtension.empty())
            return {};

        const std::vector<std::filesystem::path> searchRoots = {
            buildDirectory / "Release",
            buildDirectory / "RelWithDebInfo",
            buildDirectory / "MinSizeRel",
            buildDirectory,
            buildDirectory / "Debug",
        };

        const std::vector<std::string> moduleNames = {
            std::string("GameModule") + libraryExtension,
            std::string("libGameModule") + libraryExtension};

        for (const auto &searchRoot : searchRoots)
        {
            if (!std::filesystem::exists(searchRoot))
                continue;

            for (const auto &moduleName : moduleNames)
            {
                const auto candidatePath = searchRoot / moduleName;
                if (std::filesystem::exists(candidatePath) && std::filesystem::is_regular_file(candidatePath))
                    return candidatePath;
            }
        }

        std::filesystem::path bestMatch;
        int bestPreference = std::numeric_limits<int>::max();

        for (const auto &entry : std::filesystem::recursive_directory_iterator(buildDirectory))
        {
            if (!entry.is_regular_file())
                continue;

            const auto path = entry.path();
            if (path.extension() != libraryExtension)
                continue;

            const std::string stem = path.stem().string();
            if (stem == "GameModule" || stem == "libGameModule")
            {
                const int candidatePreference = buildArtifactPreference(path);
                if (candidatePreference < bestPreference ||
                    (candidatePreference == bestPreference && path.string() < bestMatch.string()))
                {
                    bestPreference = candidatePreference;
                    bestMatch = path;
                }
                continue;
            }

            if (path.filename().string().find("GameModule") != std::string::npos)
            {
                const int candidatePreference = buildArtifactPreference(path);
                if (candidatePreference < bestPreference ||
                    (candidatePreference == bestPreference && path.string() < bestMatch.string()))
                {
                    bestPreference = candidatePreference;
                    bestMatch = path;
                }
            }
        }

        return bestMatch;
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

        if (project.projectLibrary)
        {
            elix::engine::ScriptsRegister::setActiveRegister(nullptr);
            elix::engine::PluginLoader::closeLibrary(project.projectLibrary);
            project.projectLibrary = nullptr;
        }

        project.projectLibrary = elix::engine::PluginLoader::loadLibrary(moduleLibraryPath.string());
        if (!project.projectLibrary)
        {
            elix::engine::ScriptsRegister::setActiveRegister(nullptr);
            VX_EDITOR_ERROR_STREAM("Failed to load game module on startup: " << moduleLibraryPath << '\n');
            return false;
        }

        auto getScriptsRegisterFunction = elix::engine::PluginLoader::getFunction<elix::engine::ScriptsRegister &(*)()>(
            "getScriptsRegister",
            project.projectLibrary);
        if (!getScriptsRegisterFunction)
        {
            elix::engine::PluginLoader::closeLibrary(project.projectLibrary);
            project.projectLibrary = nullptr;
            elix::engine::ScriptsRegister::setActiveRegister(nullptr);
            VX_EDITOR_ERROR_STREAM("Game module loaded but getScriptsRegister symbol is missing: " << moduleLibraryPath << '\n');
            return false;
        }

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

    m_projectPath = std::filesystem::absolute(config.getArgs()[1]).string();
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
    m_project = ProjectLoader::loadProject(m_projectPath);

    if (!m_project)
    {
        VX_DEV_ERROR_STREAM("Failed to load project\n");
        return false;
    }

    engine::AssetsLoader::setTextureAssetImportRootDirectory(std::filesystem::path(m_project->fullPath));

    engine::ScriptsRegister *startupScriptsRegister = nullptr;
    std::string startupModulePath;
    tryLoadProjectScriptsModule(*m_project, startupScriptsRegister, startupModulePath);

    m_editorScene = std::make_shared<engine::Scene>();
    m_activeScene = m_editorScene;
    m_stillLoadingTheScene = true;

    const std::string entryScenePath = m_project->entryScene;
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
    m_editor->setDockingFullscreen(true);
    m_editor->setReloadShadersCallback([this](std::vector<std::string> *outErrors) -> size_t
                                       { return m_shaderHotReloader ? m_shaderHotReloader->recompileAll(outErrors) : 0; });
    // Must be set BEFORE initEditorRenderGraph(), which triggers initStyle() → creates m_assetsWindow.
    m_editor->setOnSceneOpenRequest([this](const std::filesystem::path &path)
                                    { openSceneFromFile(path); });

    initEditorRenderGraph();
    m_editorRenderGraphTopologyHash = renderGraphTopologyHash();

    m_editor->addOnViewportChangedCallback(std::bind(&EditorRuntime::applyEditorViewportExtent, this, std::placeholders::_1,
                                                     std::placeholders::_2));

    m_editor->addOnGameViewportChangedCallback(std::bind(&EditorRuntime::applyGameViewportExtent, this, std::placeholders::_1,
                                                         std::placeholders::_2));

    applyEditorViewportExtent(m_editor->getViewportX(), m_editor->getViewportY());
    applyGameViewportExtent(m_editor->getGameViewportX(), m_editor->getGameViewportY());

    m_editor->setScene(m_activeScene);
    m_editor->setProject(m_project);
    m_editor->setProjectScriptsRegister(startupScriptsRegister, startupModulePath);
    m_editor->setRenderViewportOnly(true);

    m_editorRenderCamera = m_editor->getCurrentCamera();
    if (m_editorRenderCamera)
        m_editorRenderCamera->setPosition({0.0f, 1.0f, 5.0f});

    switchActiveScene(m_editorScene);

    m_editor->addOnModeChangedCallback(std::bind(&EditorRuntime::onEditorModeChanged, this, std::placeholders::_1));

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
    m_stillLoadingTheScene = true;
    m_loadingStartedAt = std::chrono::steady_clock::now();
    m_loadingSceneName = path.filename().string();
    setLoadingStatus("Preparing async scene loading...");
    m_editor->setRenderViewportOnly(true);

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

void EditorRuntime::initEditorRenderGraph()
{
    if (m_renderGraph)
        m_renderGraph->cleanResources();

    // Reset extent cache so applyEditorViewportExtent() always re-applies extents to
    // newly created pass objects (they initialize from swapchain extent in their ctors).
    m_lastEditorRenderExtent = {};
    m_lastGameRenderExtent = {};

    m_renderGraph = std::make_unique<engine::renderGraph::RenderGraph>(true);
    m_depthPrepassRenderGraphPass = nullptr;
    m_rtShadowsRenderGraphPass = nullptr;
    m_rtShadowDenoiseRenderGraphPass = nullptr;
    m_rtaoRenderGraphPass = nullptr;
    m_rtReflectionsRenderGraphPass = nullptr;
    m_rtReflectionDenoiseRenderGraphPass = nullptr;

    const auto context = core::VulkanContext::getContext();
    const bool supportsRayQuery = context && context->hasRayQuerySupport();
    const bool supportsRayPipeline = context && context->hasRayTracingPipelineSupport();
    const bool supportsAnyRT = supportsRayQuery || supportsRayPipeline;
    const auto &settings = engine::RenderQualitySettings::getInstance();
    const bool useRTShadows = settings.enableRayTracing && settings.enableRTShadows && supportsAnyRT;
    const bool useRTAO = settings.enableRayTracing && settings.enableRTAO && supportsRayQuery;
    const bool useRTReflections = settings.enableRayTracing && settings.enableRTReflections && supportsAnyRT;

    m_gBufferRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::GBufferRenderGraphPass>();
    m_shadowRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::ShadowRenderGraphPass>();

    m_ssaoRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::SSAORenderGraphPass>(
        m_gBufferRenderGraphPass->getDepthTextureHandler(),
        m_gBufferRenderGraphPass->getNormalTextureHandlers());

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

    if (useRTAO)
    {
        m_rtaoRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::RTAORenderGraphPass>(
            m_gBufferRenderGraphPass->getDepthTextureHandler(),
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_ssaoRenderGraphPass->getAOHandlers());
    }

    auto *rtShadowHandlers = m_rtShadowDenoiseRenderGraphPass ? &m_rtShadowDenoiseRenderGraphPass->getOutput() : nullptr;
    auto *aoHandlers = m_rtaoRenderGraphPass ? &m_rtaoRenderGraphPass->getAOHandlers()
                                             : &m_ssaoRenderGraphPass->getAOHandlers();

    m_lightingRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::LightingRenderGraphPass>(
        m_shadowRenderGraphPass->getDirectionalShadowHandler(),
        m_gBufferRenderGraphPass->getDepthTextureHandler(),
        m_shadowRenderGraphPass->getCubeShadowHandler(),
        m_shadowRenderGraphPass->getSpotShadowHandler(),
        m_gBufferRenderGraphPass->getAlbedoTextureHandlers(),
        m_gBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gBufferRenderGraphPass->getMaterialTextureHandlers(),
        m_gBufferRenderGraphPass->getEmissiveTextureHandlers(),
        m_gBufferRenderGraphPass->getTangentAnisoTextureHandlers(),
        rtShadowHandlers,
        aoHandlers);

    m_contactShadowRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::ContactShadowRenderGraphPass>(
        m_lightingRenderGraphPass->getOutput(),
        m_gBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gBufferRenderGraphPass->getDepthTextureHandler());

    m_skyLightRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::SkyLightRenderGraphPass>(
        m_contactShadowRenderGraphPass->getOutput(),
        m_gBufferRenderGraphPass->getDepthTextureHandler());

    auto *sceneColorInput = &m_skyLightRenderGraphPass->getOutput();
    if (useRTReflections)
    {
        m_rtReflectionsRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::RTReflectionsRenderGraphPass>(
            *sceneColorInput,
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gBufferRenderGraphPass->getAlbedoTextureHandlers(),
            m_gBufferRenderGraphPass->getMaterialTextureHandlers(),
            m_gBufferRenderGraphPass->getDepthTextureHandler());
        m_rtReflectionDenoiseRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::RTReflectionDenoiseRenderGraphPass>(
            m_rtReflectionsRenderGraphPass->getOutput(),
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gBufferRenderGraphPass->getDepthTextureHandler());
        sceneColorInput = &m_rtReflectionDenoiseRenderGraphPass->getOutput();
    }

    m_particleRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::ParticleRenderGraphPass>(
        *sceneColorInput,
        &m_gBufferRenderGraphPass->getDepthTextureHandler());
    m_particleRenderGraphPass->setScene(m_activeScene.get());

    m_bloomRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::BloomRenderGraphPass>(
        m_particleRenderGraphPass->getHandlers());

    m_tonemapRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::TonemapRenderGraphPass>(
        m_particleRenderGraphPass->getHandlers());

    m_bloomCompositeRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::BloomCompositeRenderGraphPass>(
        m_tonemapRenderGraphPass->getHandlers(),
        m_bloomRenderGraphPass->getHandlers());

    m_fxaaRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::FXAARenderGraphPass>(
        m_bloomCompositeRenderGraphPass->getHandlers());

    m_smaaRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::SMAAPassRenderGraphPass>(
        m_fxaaRenderGraphPass->getHandlers());

    m_cinematicEffectsRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::CinematicEffectsRenderGraphPass>(
        m_smaaRenderGraphPass->getHandlers());

    m_selectionOverlayRenderGraphPass = m_renderGraph->addPass<SelectionOverlayRenderGraphPass>(
        m_editor,
        m_cinematicEffectsRenderGraphPass->getHandlers(),
        m_gBufferRenderGraphPass->getObjectTextureHandler());

    m_uiRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::UIRenderGraphPass>(
        m_selectionOverlayRenderGraphPass->getHandlers());

    m_editorBillboardRenderGraphPass = m_renderGraph->addPass<EditorBillboardRenderGraphPass>(
        m_activeScene,
        m_uiRenderGraphPass->getHandlers());
    m_editorBillboardRenderGraphPass->setCameraIconTexturePath("./resources/textures/velix_logo.tex.elixasset");
    m_editorBillboardRenderGraphPass->setLightIconTexturePath("./resources/textures/velix_logo.tex.elixasset");
    m_editorBillboardRenderGraphPass->setAudioIconTexturePath("./resources/textures/velix_logo.tex.elixasset");

    m_imGuiRenderGraphPass = m_renderGraph->addPass<ImGuiRenderGraphPass>(
        m_editor,
        m_editorBillboardRenderGraphPass->getHandlers(),
        m_gBufferRenderGraphPass->getObjectTextureHandler());

    VkExtent2D previewExtent{.width = 256, .height = 256};
    m_previewAssetsRenderGraphPass = m_renderGraph->addPass<PreviewAssetsRenderGraphPass>(previewExtent);

    m_renderGraph->setup();
    m_renderGraph->createRenderGraphResources();
    m_editorRenderGraphTopologyHash = renderGraphTopologyHash();
}

void EditorRuntime::initGameViewportRenderGraph()
{
    if (m_gameViewportRenderGraph)
        m_gameViewportRenderGraph->cleanResources();

    m_lastGameRenderExtent = {};

    m_gameViewportRenderGraph = std::make_unique<engine::renderGraph::RenderGraph>(false);
    m_gameDepthPrepassRenderGraphPass = nullptr;
    m_gameRTShadowsRenderGraphPass = nullptr;
    m_gameRTShadowDenoiseRenderGraphPass = nullptr;
    m_gameRtaoRenderGraphPass = nullptr;
    m_gameRTReflectionsRenderGraphPass = nullptr;
    m_gameRtReflectionDenoiseRenderGraphPass = nullptr;

    const auto context = core::VulkanContext::getContext();
    const bool supportsRayQuery = context && context->hasRayQuerySupport();
    const bool supportsRayPipeline = context && context->hasRayTracingPipelineSupport();
    const bool supportsAnyRT = supportsRayQuery || supportsRayPipeline;
    const auto &settings = engine::RenderQualitySettings::getInstance();
    const bool useRTShadows = settings.enableRayTracing && settings.enableRTShadows && supportsAnyRT;
    const bool useRTAO = settings.enableRayTracing && settings.enableRTAO && supportsRayQuery;
    const bool useRTReflections = settings.enableRayTracing && settings.enableRTReflections && supportsAnyRT;

    m_gameGBufferRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::GBufferRenderGraphPass>();
    m_gameShadowRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::ShadowRenderGraphPass>();

    m_gameSSAORenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::SSAORenderGraphPass>(
        m_gameGBufferRenderGraphPass->getDepthTextureHandler(),
        m_gameGBufferRenderGraphPass->getNormalTextureHandlers());

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

    if (useRTAO)
    {
        m_gameRtaoRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::RTAORenderGraphPass>(
            m_gameGBufferRenderGraphPass->getDepthTextureHandler(),
            m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gameSSAORenderGraphPass->getAOHandlers());
    }

    auto *gameRTShadowHandlers = m_gameRTShadowDenoiseRenderGraphPass ? &m_gameRTShadowDenoiseRenderGraphPass->getOutput() : nullptr;
    auto *gameAOHandlers = m_gameRtaoRenderGraphPass ? &m_gameRtaoRenderGraphPass->getAOHandlers()
                                                     : &m_gameSSAORenderGraphPass->getAOHandlers();

    m_gameLightingRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::LightingRenderGraphPass>(
        m_gameShadowRenderGraphPass->getDirectionalShadowHandler(),
        m_gameGBufferRenderGraphPass->getDepthTextureHandler(),
        m_gameShadowRenderGraphPass->getCubeShadowHandler(),
        m_gameShadowRenderGraphPass->getSpotShadowHandler(),
        m_gameGBufferRenderGraphPass->getAlbedoTextureHandlers(),
        m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gameGBufferRenderGraphPass->getMaterialTextureHandlers(),
        m_gameGBufferRenderGraphPass->getEmissiveTextureHandlers(),
        m_gameGBufferRenderGraphPass->getTangentAnisoTextureHandlers(),
        gameRTShadowHandlers,
        gameAOHandlers);

    m_gameContactShadowRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::ContactShadowRenderGraphPass>(
        m_gameLightingRenderGraphPass->getOutput(),
        m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gameGBufferRenderGraphPass->getDepthTextureHandler());

    m_gameSkyLightRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::SkyLightRenderGraphPass>(
        m_gameContactShadowRenderGraphPass->getOutput(),
        m_gameGBufferRenderGraphPass->getDepthTextureHandler());

    auto *gameSceneColorInput = &m_gameSkyLightRenderGraphPass->getOutput();
    if (useRTReflections)
    {
        m_gameRTReflectionsRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::RTReflectionsRenderGraphPass>(
            *gameSceneColorInput,
            m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gameGBufferRenderGraphPass->getAlbedoTextureHandlers(),
            m_gameGBufferRenderGraphPass->getMaterialTextureHandlers(),
            m_gameGBufferRenderGraphPass->getDepthTextureHandler());
        m_gameRtReflectionDenoiseRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::RTReflectionDenoiseRenderGraphPass>(
            m_gameRTReflectionsRenderGraphPass->getOutput(),
            m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gameGBufferRenderGraphPass->getDepthTextureHandler());
        gameSceneColorInput = &m_gameRtReflectionDenoiseRenderGraphPass->getOutput();
    }

    m_gameParticleRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::ParticleRenderGraphPass>(
        *gameSceneColorInput,
        &m_gameGBufferRenderGraphPass->getDepthTextureHandler());
    m_gameParticleRenderGraphPass->setScene(m_activeScene.get());

    m_gameBloomRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::BloomRenderGraphPass>(
        m_gameParticleRenderGraphPass->getHandlers());

    m_gameTonemapRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::TonemapRenderGraphPass>(
        m_gameParticleRenderGraphPass->getHandlers());

    m_gameBloomCompositeRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::BloomCompositeRenderGraphPass>(
        m_gameTonemapRenderGraphPass->getHandlers(),
        m_gameBloomRenderGraphPass->getHandlers());

    m_gameFXAARenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::FXAARenderGraphPass>(
        m_gameBloomCompositeRenderGraphPass->getHandlers());

    m_gameSMAARenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::SMAAPassRenderGraphPass>(
        m_gameFXAARenderGraphPass->getHandlers());

    m_gameCinematicEffectsRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::CinematicEffectsRenderGraphPass>(
        m_gameSMAARenderGraphPass->getHandlers());

    m_gameUIRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::UIRenderGraphPass>(
        m_gameCinematicEffectsRenderGraphPass->getHandlers());

    m_gameViewportRenderGraph->setup();
    m_gameViewportRenderGraph->createRenderGraphResources();
    m_gameViewportRenderGraphTopologyHash = renderGraphTopologyHash();
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
                switchActiveScene(m_editorScene);
                VX_EDITOR_INFO_STREAM("Scene loading completed: " << m_project->entryScene << '\n');
            }
            else
            {
                VX_EDITOR_ERROR_STREAM("Scene loading failed, keeping empty editor scene.\n");
            }

            m_stillLoadingTheScene = false;
            m_editor->setRenderViewportOnly(false);
            m_editor->setDockingFullscreen(true);
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
    if (shouldRenderGameViewport && !m_gameViewportRenderGraph)
        initGameViewportRenderGraph();

    const uint32_t gameViewportWidth = m_editor->getGameViewportX();
    const uint32_t gameViewportHeight = m_editor->getGameViewportY();
    applyGameViewportExtent(gameViewportWidth, gameViewportHeight);
    if (m_gameRenderCamera && gameViewportWidth > 0 && gameViewportHeight > 0)
        m_gameRenderCamera->setAspect(static_cast<float>(gameViewportWidth) / static_cast<float>(gameViewportHeight));

    const size_t currentTopologyHash = renderGraphTopologyHash();
    if (currentTopologyHash != m_editorRenderGraphTopologyHash)
    {
        initEditorRenderGraph();
        m_editorRenderGraphTopologyHash = currentTopologyHash;
        applyEditorViewportExtent(editorViewportWidth, editorViewportHeight);
    }

    if (shouldRenderGameViewport && currentTopologyHash != m_gameViewportRenderGraphTopologyHash)
    {
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

    if (m_shadowRenderGraphPass)
        m_shadowRenderGraphPass->syncQualitySettings();
    if (m_gameShadowRenderGraphPass)
        m_gameShadowRenderGraphPass->syncQualitySettings();

    if (m_gameViewportRenderGraph && m_gameRenderCamera && shouldRenderGameViewport)
    {
        m_gameViewportRenderGraph->prepareFrame(m_gameRenderCamera, m_activeScene.get(), deltaTime);
        m_gameViewportRenderGraph->draw();

        if (m_imGuiRenderGraphPass && m_gameUIRenderGraphPass)
            m_imGuiRenderGraphPass->setGameViewportImages(
                m_gameUIRenderGraphPass->getOutputImageViews(),
                true,
                m_gameViewportRenderGraph->getCurrentImageIndex());
    }
    else if (m_imGuiRenderGraphPass)
    {
        m_imGuiRenderGraphPass->setGameViewportImages({}, m_gameRenderCamera != nullptr, 0u);
    }

    if (m_stillLoadingTheScene)
        addLoadingRenderToImGui();

    m_renderGraph->prepareFrame(m_editorRenderCamera, m_activeScene.get(), deltaTime);
    m_editor->setRenderGraphProfilingData(m_renderGraph->getLastFrameProfilingData());
    m_renderGraph->draw();
    m_editor->processPendingObjectSelection();

    m_editor->setDonePreviewJobs(m_previewAssetsRenderGraphPass->getRenderedImages());
}

void EditorRuntime::applyEditorViewportExtent(uint32_t width, uint32_t height)
{
    const VkExtent2D extent = makeScaledRenderExtent(width, height);
    if (extent.width == m_lastEditorRenderExtent.width && extent.height == m_lastEditorRenderExtent.height)
        return;

    m_gBufferRenderGraphPass->setExtent(extent);
    if (m_rtShadowsRenderGraphPass) m_rtShadowsRenderGraphPass->setExtent(extent);
    if (m_rtShadowDenoiseRenderGraphPass) m_rtShadowDenoiseRenderGraphPass->setExtent(extent);
    m_ssaoRenderGraphPass->setExtent(extent);
    m_lightingRenderGraphPass->setExtent(extent);
    if (m_rtReflectionsRenderGraphPass) m_rtReflectionsRenderGraphPass->setExtent(extent);
    if (m_rtaoRenderGraphPass) m_rtaoRenderGraphPass->setExtent(extent);
    if (m_rtReflectionDenoiseRenderGraphPass) m_rtReflectionDenoiseRenderGraphPass->setExtent(extent);
    m_skyLightRenderGraphPass->setExtent(extent);
    m_bloomRenderGraphPass->setExtent(extent);
    m_tonemapRenderGraphPass->setExtent(extent);
    m_bloomCompositeRenderGraphPass->setExtent(extent);
    m_fxaaRenderGraphPass->setExtent(extent);
    m_smaaRenderGraphPass->setExtent(extent);
    m_contactShadowRenderGraphPass->setExtent(extent);
    m_cinematicEffectsRenderGraphPass->setExtent(extent);
    m_selectionOverlayRenderGraphPass->setExtent(extent);
    m_uiRenderGraphPass->setExtent(extent);
    m_editorBillboardRenderGraphPass->setExtent(extent);
    m_particleRenderGraphPass->setExtent(extent);

    m_lastEditorRenderExtent = extent;
}

void EditorRuntime::applyGameViewportExtent(uint32_t width, uint32_t height)
{
    if (!m_gameViewportRenderGraph || !m_gameGBufferRenderGraphPass || !m_gameUIRenderGraphPass)
        return;

    const VkExtent2D extent = makeScaledRenderExtent(width, height);
    if (extent.width == m_lastGameRenderExtent.width && extent.height == m_lastGameRenderExtent.height)
        return;

    m_gameGBufferRenderGraphPass->setExtent(extent);
    if (m_gameRTShadowsRenderGraphPass) m_gameRTShadowsRenderGraphPass->setExtent(extent);
    if (m_gameRTShadowDenoiseRenderGraphPass) m_gameRTShadowDenoiseRenderGraphPass->setExtent(extent);
    m_gameSSAORenderGraphPass->setExtent(extent);
    m_gameLightingRenderGraphPass->setExtent(extent);
    if (m_gameRTReflectionsRenderGraphPass) m_gameRTReflectionsRenderGraphPass->setExtent(extent);
    if (m_gameRtaoRenderGraphPass) m_gameRtaoRenderGraphPass->setExtent(extent);
    if (m_gameRtReflectionDenoiseRenderGraphPass) m_gameRtReflectionDenoiseRenderGraphPass->setExtent(extent);
    m_gameSkyLightRenderGraphPass->setExtent(extent);
    m_gameBloomRenderGraphPass->setExtent(extent);
    m_gameTonemapRenderGraphPass->setExtent(extent);
    m_gameBloomCompositeRenderGraphPass->setExtent(extent);
    m_gameFXAARenderGraphPass->setExtent(extent);
    m_gameSMAARenderGraphPass->setExtent(extent);
    m_gameContactShadowRenderGraphPass->setExtent(extent);
    m_gameCinematicEffectsRenderGraphPass->setExtent(extent);
    m_gameUIRenderGraphPass->setExtent(extent);
    m_gameParticleRenderGraphPass->setExtent(extent);

    m_lastGameRenderExtent = extent;
}

void EditorRuntime::shutdown()
{
    if (m_loadingFuture.valid())
        m_loadingFuture.wait();

    engine::ScriptsRegister::setActiveRegister(nullptr);

    if (m_renderGraph)
        m_renderGraph->cleanResources();

    if (m_gameViewportRenderGraph)
        m_gameViewportRenderGraph->cleanResources();

    ImGuiRenderGraphPass::shutdownPersistentImGuiBackend();

    engine::scripting::setActiveScene(nullptr);
    engine::scripting::setActiveWindow(nullptr);

    if (m_editor)
        m_editor->saveProjectConfig();

    if (m_project && m_project->projectLibrary)
    {
        engine::PluginLoader::closeLibrary(m_project->projectLibrary);
        m_project->projectLibrary = nullptr;
    }

    if (m_project)
        m_project->clearCache();

    engine::AssetsLoader::setTextureAssetImportRootDirectory({});
}

ELIX_NESTED_NAMESPACE_END
