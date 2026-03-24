#include "Engine/Runtime/GameRuntime.hpp"

#include "Core/Logger.hpp"
#include "Core/VulkanContext.hpp"

#include "Engine/Components/CameraComponent.hpp"
#include "Engine/Components/ScriptComponent.hpp"
#include "Engine/PluginSystem/PluginLoader.hpp"
#include "Engine/PluginSystem/PluginManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Scripting/ScriptsRegister.hpp"
#include "Engine/SceneManager.hpp"
#include "Engine/Scripting/VelixAPI.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace
{
    template <typename TValue>
    void hashCombine(size_t &seed, const TValue &value)
    {
        seed ^= std::hash<TValue>{}(value) + 0x9e3779b97f4a7c15ULL + (seed << 6u) + (seed >> 2u);
    }

    std::filesystem::path normalizeAbsolutePath(const std::filesystem::path &path)
    {
        std::error_code errorCode;
        const auto absolutePath = std::filesystem::absolute(path, errorCode);
        if (errorCode)
            return path.lexically_normal();

        return absolutePath.lexically_normal();
    }

    bool hasElixPacketExtension(const std::filesystem::path &path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return extension == ".elixpacket";
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
} // namespace

ELIX_NESTED_NAMESPACE_BEGIN(engine)

GameRuntime::GameRuntime(const ApplicationConfig &config)
    : m_args(config.getArgs())
{
}

bool GameRuntime::shouldRunForConfig(const ApplicationConfig &config)
{
    const auto &args = config.getArgs();
    if (args.size() <= 1u)
        return !findPacketInDirectory(currentExecutableDirectory()).empty();

    for (size_t index = 1u; index < args.size(); ++index)
    {
        const std::string lowerArgument = toLowerCopy(args[index]);
        if (lowerArgument == "--editor")
            return false;
    }

    for (size_t index = 1u; index < args.size(); ++index)
    {
        const std::string lowerArgument = toLowerCopy(args[index]);
        if (lowerArgument == "--game" || lowerArgument == "--packet")
            return true;

        if (hasElixPacketExtension(std::filesystem::path(args[index])))
            return true;
    }

    return false;
}

bool GameRuntime::init()
{
    std::error_code cwdError;
    const std::filesystem::path executableDirectory = currentExecutableDirectory();
    if (!executableDirectory.empty())
        std::filesystem::current_path(executableDirectory, cwdError);
    if (cwdError)
        VX_ENGINE_WARNING_STREAM("Failed to switch working directory to executable directory: " << cwdError.message() << '\n');

    std::string errorMessage;
    if (!resolveLaunchPaths(&errorMessage))
    {
        VX_ENGINE_ERROR_STREAM("Game runtime launch path resolve failed: " << errorMessage << '\n');
        return false;
    }

    if (!loadProjectModule(&errorMessage))
    {
        VX_ENGINE_ERROR_STREAM("Game runtime module load failed: " << errorMessage << '\n');
        return false;
    }

    {
        auto &pm = PluginManager::instance();
        pm.loadPluginsFromDirectory("resources/plugins");
        pm.loadPluginsFromDirectory(executableDirectory / "Plugins");
        pm.loadPluginsFromDirectory(m_packetPath.parent_path() / "Plugins");
    }

    if (!extractPacket(&errorMessage))
    {
        VX_ENGINE_ERROR_STREAM("Game runtime packet extraction failed: " << errorMessage << '\n');
        return false;
    }

    m_scene = std::make_shared<Scene>();
    if (!m_scene->loadSceneFromFile(m_entryScenePath.string()))
    {
        VX_ENGINE_ERROR_STREAM("Failed to load game scene: " << m_entryScenePath << '\n');
        return false;
    }

    scripting::setActiveScene(m_scene.get());

    initRenderGraph();
    m_renderGraphTopologyHash = renderGraphTopologyHash();

    forEachScriptComponent([](ScriptComponent *scriptComponent)
                           {
                               if (scriptComponent)
                                   scriptComponent->onAttach(); });
    m_scriptsAttached = true;
    m_initialized = true;

    VX_ENGINE_INFO_STREAM("Game runtime initialized from packet: " << m_packetPath << '\n');
    return true;
}

void GameRuntime::tick(float deltaTime)
{
    if (!m_initialized || !m_scene || !m_renderGraph)
        return;

    if (SceneManager::instance().hasPendingRequests())
    {
        forEachScriptComponent([](ScriptComponent *scriptComponent)
                               {
                                   if (scriptComponent)
                                       scriptComponent->onDetach(); });

        SceneManager::instance().processRequests(m_scene, [this](std::shared_ptr<Scene> /*newScene*/)
                                                 { bindSceneToPasses(); });

        forEachScriptComponent([](ScriptComponent *scriptComponent)
                               {
                                   if (scriptComponent)
                                       scriptComponent->onAttach(); });
    }

    m_scene->update(deltaTime);

    refreshActiveCamera();
    collectAndSubmitUIRenderData();
    syncViewportExtent();

    if (m_shadowRenderGraphPass)
        m_shadowRenderGraphPass->syncQualitySettings();

    const size_t currentTopologyHash = renderGraphTopologyHash();
    if (currentTopologyHash != m_renderGraphTopologyHash)
    {
        initRenderGraph();
        m_renderGraphTopologyHash = currentTopologyHash;
    }

    m_renderGraph->prepareFrame(m_renderCamera, m_scene.get(), deltaTime);
    m_renderGraph->draw();
}

void GameRuntime::initRenderGraph()
{
    if (m_renderGraph)
        m_renderGraph->cleanResources();

    m_renderGraph = std::make_unique<renderGraph::RenderGraph>(true);

    m_depthPrepassRenderGraphPass = nullptr;
    m_gBufferRenderGraphPass = nullptr;
    m_shadowRenderGraphPass = nullptr;
    m_rtShadowsRenderGraphPass = nullptr;
    m_rtShadowDenoiseRenderGraphPass = nullptr;
    m_ssaoRenderGraphPass = nullptr;
    m_rtaoRenderGraphPass = nullptr;
    m_lightingRenderGraphPass = nullptr;
    m_contactShadowRenderGraphPass = nullptr;
    m_skyLightRenderGraphPass = nullptr;
    m_rtReflectionsRenderGraphPass = nullptr;
    m_rtReflectionDenoiseRenderGraphPass = nullptr;
    m_particleRenderGraphPass = nullptr;
    m_bloomRenderGraphPass = nullptr;
    m_tonemapRenderGraphPass = nullptr;
    m_bloomCompositeRenderGraphPass = nullptr;
    m_fxaaRenderGraphPass = nullptr;
    m_smaaRenderGraphPass = nullptr;
    m_taaRenderGraphPass = nullptr;
    m_uiRenderGraphPass = nullptr;
    m_presentRenderGraphPass = nullptr;

    const auto &settings = RenderQualitySettings::getInstance();
    const auto context = core::VulkanContext::getContext();
    const bool supportsRayQuery = context && context->hasRayQuerySupport();
    const bool supportsRayPipeline = context && context->hasRayTracingPipelineSupport();
    const bool supportsAnyRT = supportsRayQuery || supportsRayPipeline;
    const bool useRTShadows = settings.enableRayTracing && settings.enableRTShadows && supportsAnyRT;
    const bool useRTAO = settings.enableRayTracing && settings.enableRTAO && supportsRayQuery;
    const bool useRTReflections = settings.enableRayTracing && settings.enableRTReflections && supportsAnyRT;

    m_gBufferRenderGraphPass = m_renderGraph->addPass<renderGraph::GBufferRenderGraphPass>(false);
    m_shadowRenderGraphPass = m_renderGraph->addPass<renderGraph::ShadowRenderGraphPass>();

    m_ssaoRenderGraphPass = m_renderGraph->addPass<renderGraph::SSAORenderGraphPass>(
        m_gBufferRenderGraphPass->getDepthTextureHandler(),
        m_gBufferRenderGraphPass->getNormalTextureHandlers());

    if (useRTShadows)
    {
        m_rtShadowsRenderGraphPass = m_renderGraph->addPass<renderGraph::RTShadowsRenderGraphPass>(
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gBufferRenderGraphPass->getDepthTextureHandler());
        m_rtShadowDenoiseRenderGraphPass = m_renderGraph->addPass<renderGraph::RTShadowDenoiseRenderGraphPass>(
            m_rtShadowsRenderGraphPass->getOutput(),
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gBufferRenderGraphPass->getDepthTextureHandler());
    }

    if (useRTAO)
    {
        m_rtaoRenderGraphPass = m_renderGraph->addPass<renderGraph::RTAORenderGraphPass>(
            m_gBufferRenderGraphPass->getDepthTextureHandler(),
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_ssaoRenderGraphPass->getAOHandlers());
    }

    auto *rtShadowHandlers = m_rtShadowDenoiseRenderGraphPass ? &m_rtShadowDenoiseRenderGraphPass->getOutput() : nullptr;
    auto *aoHandlers = m_rtaoRenderGraphPass ? &m_rtaoRenderGraphPass->getAOHandlers()
                                             : &m_ssaoRenderGraphPass->getAOHandlers();

    m_lightingRenderGraphPass = m_renderGraph->addPass<renderGraph::LightingRenderGraphPass>(
        m_shadowRenderGraphPass->getDirectionalShadowHandler(),
        m_gBufferRenderGraphPass->getDepthTextureHandler(),
        m_shadowRenderGraphPass->getCubeShadowHandler(),
        m_shadowRenderGraphPass->getSpotShadowHandler(),
        m_gBufferRenderGraphPass->getAlbedoTextureHandlers(),
        m_gBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gBufferRenderGraphPass->getMaterialTextureHandlers(),
        m_gBufferRenderGraphPass->getEmissiveTextureHandlers(),
        rtShadowHandlers,
        aoHandlers);

    m_contactShadowRenderGraphPass = m_renderGraph->addPass<renderGraph::ContactShadowRenderGraphPass>(
        m_lightingRenderGraphPass->getOutput(),
        m_gBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gBufferRenderGraphPass->getDepthTextureHandler());

    m_skyLightRenderGraphPass = m_renderGraph->addPass<renderGraph::SkyLightRenderGraphPass>(
        m_contactShadowRenderGraphPass->getOutput(),
        m_gBufferRenderGraphPass->getDepthTextureHandler());

    auto *sceneColorInput = &m_skyLightRenderGraphPass->getOutput();
    if (useRTReflections)
    {
        m_rtReflectionsRenderGraphPass = m_renderGraph->addPass<renderGraph::RTReflectionsRenderGraphPass>(
            *sceneColorInput,
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gBufferRenderGraphPass->getAlbedoTextureHandlers(),
            m_gBufferRenderGraphPass->getMaterialTextureHandlers(),
            m_gBufferRenderGraphPass->getDepthTextureHandler());
        m_rtReflectionDenoiseRenderGraphPass = m_renderGraph->addPass<renderGraph::RTReflectionDenoiseRenderGraphPass>(
            m_rtReflectionsRenderGraphPass->getOutput(),
            m_gBufferRenderGraphPass->getNormalTextureHandlers(),
            m_gBufferRenderGraphPass->getDepthTextureHandler());
        sceneColorInput = &m_rtReflectionDenoiseRenderGraphPass->getOutput();
    }

    m_particleRenderGraphPass = m_renderGraph->addPass<renderGraph::ParticleRenderGraphPass>(
        *sceneColorInput,
        &m_gBufferRenderGraphPass->getDepthTextureHandler());

    m_bloomRenderGraphPass = m_renderGraph->addPass<renderGraph::BloomRenderGraphPass>(
        m_particleRenderGraphPass->getHandlers());

    m_tonemapRenderGraphPass = m_renderGraph->addPass<renderGraph::TonemapRenderGraphPass>(
        m_particleRenderGraphPass->getHandlers());

    m_bloomCompositeRenderGraphPass = m_renderGraph->addPass<renderGraph::BloomCompositeRenderGraphPass>(
        m_tonemapRenderGraphPass->getHandlers(),
        m_bloomRenderGraphPass->getHandlers());

    m_fxaaRenderGraphPass = m_renderGraph->addPass<renderGraph::FXAARenderGraphPass>(
        m_bloomCompositeRenderGraphPass->getHandlers());

    m_smaaRenderGraphPass = m_renderGraph->addPass<renderGraph::SMAAPassRenderGraphPass>(
        m_fxaaRenderGraphPass->getHandlers());

    m_taaRenderGraphPass = m_renderGraph->addPass<renderGraph::TAARenderGraphPass>(
        m_smaaRenderGraphPass->getHandlers());

    m_uiRenderGraphPass = m_renderGraph->addPass<renderGraph::UIRenderGraphPass>(
        m_taaRenderGraphPass->getHandlers());

    m_presentRenderGraphPass = m_renderGraph->addPass<renderGraph::PresentRenderGraphPass>(
        m_uiRenderGraphPass->getHandlers());

    bindSceneToPasses();

    m_renderGraph->setup();
    m_renderGraph->createRenderGraphResources();
    m_lastExtent = {0u, 0u};
    syncViewportExtent();
}

void GameRuntime::shutdown()
{
    ScriptsRegister::setActiveRegister(nullptr);

    PluginManager::instance().unloadAll();

    if (m_scriptsAttached)
    {
        forEachScriptComponent([](ScriptComponent *scriptComponent)
                               {
                                   if (scriptComponent)
                                       scriptComponent->onDetach(); });
        m_scriptsAttached = false;
    }

    if (m_renderGraph)
    {
        m_renderGraph->cleanResources();
        m_renderGraph.reset();
    }

    m_scene.reset();
    m_renderCamera.reset();

    scripting::setActiveScene(nullptr);
    scripting::setActiveWindow(nullptr);

    if (m_gameModuleLibrary)
    {
        PluginLoader::closeLibrary(m_gameModuleLibrary);
        m_gameModuleLibrary = nullptr;
    }

    for (auto it = m_preloadedRuntimeLibraries.rbegin(); it != m_preloadedRuntimeLibraries.rend(); ++it)
        PluginLoader::closeLibrary(*it);
    m_preloadedRuntimeLibraries.clear();

    if (!m_extractionDirectory.empty())
    {
        std::error_code removeError;
        std::filesystem::remove_all(m_extractionDirectory, removeError);
        if (removeError)
            VX_ENGINE_WARNING_STREAM("Failed to clean packet extraction directory '" << m_extractionDirectory << "': " << removeError.message() << '\n');
    }

    m_initialized = false;
}

bool GameRuntime::resolveLaunchPaths(std::string *errorMessage)
{
    const std::filesystem::path executableDirectory = currentExecutableDirectory();
    if (executableDirectory.empty())
    {
        if (errorMessage)
            *errorMessage = "Failed to resolve executable directory.";
        return false;
    }

    bool packetFromOption = false;
    bool moduleFromOption = false;

    for (size_t index = 1u; index < m_args.size(); ++index)
    {
        const std::string lowerArgument = toLowerCopy(m_args[index]);
        if (lowerArgument == "--packet")
        {
            if (index + 1u < m_args.size())
            {
                m_packetPath = m_args[index + 1u];
                packetFromOption = true;
            }
            continue;
        }

        if (lowerArgument == "--module")
        {
            if (index + 1u < m_args.size())
            {
                m_modulePath = m_args[index + 1u];
                moduleFromOption = true;
            }
            continue;
        }
    }

    if (m_packetPath.empty())
    {
        for (size_t index = 1u; index < m_args.size(); ++index)
        {
            const std::filesystem::path argumentPath(m_args[index]);
            if (hasElixPacketExtension(argumentPath))
            {
                m_packetPath = argumentPath;
                break;
            }
        }
    }

    if (m_packetPath.empty())
        m_packetPath = findPacketInDirectory(executableDirectory);

    if (m_packetPath.empty())
    {
        if (errorMessage)
            *errorMessage = "No .elixpacket found. Pass --packet <file> or place one packet next to executable.";
        return false;
    }

    if (m_packetPath.is_relative())
        m_packetPath = executableDirectory / m_packetPath;
    m_packetPath = normalizeAbsolutePath(m_packetPath);

    if (!std::filesystem::exists(m_packetPath) || !std::filesystem::is_regular_file(m_packetPath))
    {
        if (errorMessage)
            *errorMessage = "Packet file was not found: " + m_packetPath.string();
        return false;
    }

    if (m_modulePath.empty())
        m_modulePath = findGameModuleInDirectory(executableDirectory);

    if (!m_modulePath.empty())
    {
        if (m_modulePath.is_relative())
            m_modulePath = executableDirectory / m_modulePath;
        m_modulePath = normalizeAbsolutePath(m_modulePath);

        if (!std::filesystem::exists(m_modulePath) || !std::filesystem::is_regular_file(m_modulePath))
        {
            if (moduleFromOption)
            {
                if (errorMessage)
                    *errorMessage = "Module file was not found: " + m_modulePath.string();
                return false;
            }

            VX_ENGINE_WARNING_STREAM("Game module path is invalid, scripts will be unavailable: " << m_modulePath << '\n');
            m_modulePath.clear();
        }
    }
    else if (moduleFromOption)
    {
        if (errorMessage)
            *errorMessage = "Module option was provided but no module path value was found.";
        return false;
    }

    std::error_code tempPathError;
    std::filesystem::path tempDirectory = std::filesystem::temp_directory_path(tempPathError);
    if (tempPathError || tempDirectory.empty())
        tempDirectory = std::filesystem::current_path();

    const uint64_t timestamp = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    m_extractionDirectory = tempDirectory / ("velix_packet_runtime_" + std::to_string(timestamp));

    if (packetFromOption)
        VX_ENGINE_INFO_STREAM("Using packet from --packet: " << m_packetPath << '\n');
    else
        VX_ENGINE_INFO_STREAM("Using packet: " << m_packetPath << '\n');

    if (!m_modulePath.empty())
        VX_ENGINE_INFO_STREAM("Using game module: " << m_modulePath << '\n');
    else
        VX_ENGINE_WARNING_STREAM("No GameModule library found near executable. Script components may be skipped.\n");

    return true;
}

bool GameRuntime::loadProjectModule(std::string *errorMessage)
{
    if (m_modulePath.empty())
    {
        ScriptsRegister::setActiveRegister(nullptr);
        return true;
    }

    const std::filesystem::path moduleDirectory = m_modulePath.parent_path();
    for (const auto &runtimeLibraryPath : findVelixSdkLibrariesInDirectory(moduleDirectory))
    {
        auto runtimeLibraryHandle = PluginLoader::loadLibrary(runtimeLibraryPath.string());
        if (!runtimeLibraryHandle)
        {
            VX_ENGINE_WARNING_STREAM("Failed to preload runtime library '" << runtimeLibraryPath << "'. GameModule load may fail.\n");
            continue;
        }

        m_preloadedRuntimeLibraries.push_back(runtimeLibraryHandle);
    }

    m_gameModuleLibrary = PluginLoader::loadLibrary(m_modulePath.string());
    if (!m_gameModuleLibrary)
    {
        ScriptsRegister::setActiveRegister(nullptr);
        if (errorMessage)
            *errorMessage = "Failed to load game module: " + m_modulePath.string();
        return false;
    }

    auto getScriptsRegisterFunction = PluginLoader::getFunction<ScriptsRegister &(*)()>("getScriptsRegister", m_gameModuleLibrary);
    if (!getScriptsRegisterFunction)
    {
        PluginLoader::closeLibrary(m_gameModuleLibrary);
        m_gameModuleLibrary = nullptr;
        ScriptsRegister::setActiveRegister(nullptr);

        if (errorMessage)
            *errorMessage = "Game module is loaded but getScriptsRegister symbol is missing.";
        return false;
    }

    auto &scriptsRegister = getScriptsRegisterFunction();
    ScriptsRegister::setActiveRegister(&scriptsRegister);
    VX_ENGINE_INFO_STREAM("Loaded " << scriptsRegister.getScripts().size() << " script(s) from module.\n");
    return true;
}

bool GameRuntime::extractPacket(std::string *errorMessage)
{
    std::error_code createError;
    std::filesystem::create_directories(m_extractionDirectory, createError);
    if (createError)
    {
        if (errorMessage)
            *errorMessage = "Failed to create extraction directory: " + m_extractionDirectory.string();
        return false;
    }

    ElixPacketDeserializer deserializer;
    std::string extractionError;
    if (!deserializer.extractToDirectory(m_packetPath, m_extractionDirectory, &m_packetManifest, &extractionError))
    {
        if (errorMessage)
            *errorMessage = extractionError;
        return false;
    }

    if (m_packetManifest.entrySceneRelativePath.empty())
    {
        if (errorMessage)
            *errorMessage = "Packet manifest does not contain entry scene path.";
        return false;
    }

    m_entryScenePath = normalizeAbsolutePath(m_extractionDirectory / std::filesystem::path(m_packetManifest.entrySceneRelativePath));
    if (!std::filesystem::exists(m_entryScenePath) || !std::filesystem::is_regular_file(m_entryScenePath))
    {
        if (errorMessage)
            *errorMessage = "Entry scene was not found after extraction: " + m_entryScenePath.string();
        return false;
    }

    return true;
}

void GameRuntime::bindSceneToPasses()
{
    if (m_particleRenderGraphPass)
        m_particleRenderGraphPass->setScene(m_scene.get());
}

void GameRuntime::syncViewportExtent()
{
    auto *swapchain = core::VulkanContext::getContext()->getSwapchain().get();
    if (!swapchain)
        return;

    const VkExtent2D swapchainExtent = swapchain->getExtent();
    const float renderScale = std::clamp(RenderQualitySettings::getInstance().renderScale, 0.25f, 2.0f);

    const uint32_t scaledWidth = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<double>(std::max(1u, swapchainExtent.width)) * renderScale)));
    const uint32_t scaledHeight = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<double>(std::max(1u, swapchainExtent.height)) * renderScale)));
    const VkExtent2D extent{scaledWidth, scaledHeight};

    if (extent.width == m_lastExtent.width && extent.height == m_lastExtent.height)
        return;

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
    if (m_contactShadowRenderGraphPass)
        m_contactShadowRenderGraphPass->setExtent(extent);
    if (m_rtReflectionsRenderGraphPass)
        m_rtReflectionsRenderGraphPass->setExtent(extent);
    if (m_rtReflectionDenoiseRenderGraphPass)
        m_rtReflectionDenoiseRenderGraphPass->setExtent(extent);
    if (m_skyLightRenderGraphPass)
        m_skyLightRenderGraphPass->setExtent(extent);
    if (m_particleRenderGraphPass)
        m_particleRenderGraphPass->setExtent(extent);
    if (m_bloomRenderGraphPass)
        m_bloomRenderGraphPass->setExtent(extent);
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
    if (m_uiRenderGraphPass)
        m_uiRenderGraphPass->setExtent(extent);
    if (m_presentRenderGraphPass)
        m_presentRenderGraphPass->setExtent(extent);

    m_lastExtent = extent;
}

void GameRuntime::refreshActiveCamera()
{
    m_renderCamera = nullptr;
    if (!m_scene)
        return;

    for (const auto &entity : m_scene->getEntities())
    {
        if (!entity || !entity->isEnabled())
            continue;

        if (auto *cameraComponent = entity->getComponent<CameraComponent>())
        {
            m_renderCamera = cameraComponent->getCamera();
            break;
        }
    }

    if (!m_renderCamera)
        return;

    const auto extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();
    if (extent.width > 0u && extent.height > 0u)
        m_renderCamera->setAspect(static_cast<float>(extent.width) / static_cast<float>(extent.height));
}

void GameRuntime::collectAndSubmitUIRenderData()
{
    if (!m_uiRenderGraphPass || !m_scene)
        return;

    ui::UIRenderData uiData;
    for (const auto &text : m_scene->getUITexts())
    {
        if (text && text->isEnabled())
            uiData.texts.push_back(text.get());
    }

    for (const auto &button : m_scene->getUIButtons())
    {
        if (button && button->isEnabled())
            uiData.buttons.push_back(button.get());
    }

    for (const auto &billboard : m_scene->getBillboards())
    {
        if (billboard && billboard->isEnabled())
            uiData.billboards.push_back(billboard.get());
    }

    m_uiRenderGraphPass->setRenderData(uiData);
}

void GameRuntime::forEachScriptComponent(const std::function<void(ScriptComponent *)> &function)
{
    if (!m_scene || !function)
        return;

    for (const auto &entity : m_scene->getEntities())
    {
        if (!entity || !entity->isEnabled())
            continue;

        for (auto *scriptComponent : entity->getComponents<ScriptComponent>())
            function(scriptComponent);
    }
}

std::filesystem::path GameRuntime::currentExecutablePath()
{
#if defined(_WIN32)
    char buffer[MAX_PATH];
    const DWORD size = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (size == 0 || size == MAX_PATH)
        return {};
    return std::filesystem::path(buffer);
#elif defined(__linux__)
    char buffer[1024];
    const ssize_t size = readlink("/proc/self/exe", buffer, sizeof(buffer));
    if (size <= 0 || size >= static_cast<ssize_t>(sizeof(buffer)))
        return {};
    return std::filesystem::path(std::string(buffer, static_cast<size_t>(size)));
#else
    return {};
#endif
}

std::filesystem::path GameRuntime::currentExecutableDirectory()
{
    const std::filesystem::path executablePath = currentExecutablePath();
    if (executablePath.empty())
        return {};
    return executablePath.parent_path();
}

std::filesystem::path GameRuntime::findPacketInDirectory(const std::filesystem::path &directory)
{
    if (directory.empty() || !std::filesystem::exists(directory))
        return {};

    std::vector<std::filesystem::path> packetCandidates;
    for (const auto &entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
            continue;

        const auto path = entry.path();
        if (!hasElixPacketExtension(path))
            continue;

        packetCandidates.push_back(path);
    }

    if (packetCandidates.empty())
        return {};

    std::sort(packetCandidates.begin(), packetCandidates.end(), [](const auto &lhs, const auto &rhs)
              { return lhs.filename().string() < rhs.filename().string(); });

    return packetCandidates.front();
}

std::filesystem::path GameRuntime::findGameModuleInDirectory(const std::filesystem::path &directory)
{
    if (directory.empty() || !std::filesystem::exists(directory))
        return {};

    const std::string libraryExtension = sharedLibraryExtension();

    const std::vector<std::string> preferredNames = {
        std::string("GameModule") + libraryExtension,
        std::string("libGameModule") + libraryExtension};

    for (const auto &fileName : preferredNames)
    {
        const auto candidatePath = directory / fileName;
        if (std::filesystem::exists(candidatePath) && std::filesystem::is_regular_file(candidatePath))
            return candidatePath;
    }

    std::vector<std::filesystem::path> moduleCandidates;
    for (const auto &entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
            continue;

        const auto path = entry.path();
        if (path.extension() != libraryExtension)
            continue;

        if (path.filename().string().find("GameModule") != std::string::npos)
            moduleCandidates.push_back(path);
    }

    if (moduleCandidates.empty())
        return {};

    std::sort(moduleCandidates.begin(), moduleCandidates.end(), [](const auto &lhs, const auto &rhs)
              { return lhs.filename().string() < rhs.filename().string(); });
    return moduleCandidates.front();
}

std::vector<std::filesystem::path> GameRuntime::findVelixSdkLibrariesInDirectory(const std::filesystem::path &directory)
{
    std::vector<std::filesystem::path> libraries;
    if (directory.empty() || !std::filesystem::exists(directory))
        return libraries;

    const std::string libraryExtension = sharedLibraryExtension();

    for (const auto &entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
            continue;

        const auto path = entry.path();
        const std::string fileName = path.filename().string();
        if (fileName.find("VelixSDK") == std::string::npos)
            continue;

#if defined(_WIN32)
        if (path.extension() != ".dll")
            continue;
#else
        if (fileName.find(".so") == std::string::npos)
            continue;
#endif

        if (!libraryExtension.empty() && path.extension() != libraryExtension && fileName.find(libraryExtension) == std::string::npos)
            continue;

        libraries.push_back(path);
    }

    std::sort(libraries.begin(), libraries.end(), [](const auto &lhs, const auto &rhs)
              { return lhs.filename().string() < rhs.filename().string(); });
    libraries.erase(std::unique(libraries.begin(), libraries.end()), libraries.end());
    return libraries;
}

std::string GameRuntime::toLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                   { return static_cast<char>(std::tolower(character)); });
    return value;
}

ELIX_NESTED_NAMESPACE_END
