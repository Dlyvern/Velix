#include "Engine/Runtime/ApplicationLoop.hpp"

#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Assets/OBJAssetLoader.hpp"
#include "Engine/Assets/FBXAssetLoader.hpp"
#include "Engine/Assets/MaterialAssetLoader.hpp"
#include "Engine/Assets/TerrainAssetLoader.hpp"
#include "Engine/Physics/PhysXCore.hpp"
#include "Engine/Components/ReflectionProbeComponent.hpp"
#include "Engine/Scripting/VelixAPI.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Caches/GraphicsPipelineCache.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Scoped/ScopedTimer.hpp"
#include "Engine/Utilities/AsyncGpuUpload.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Runtime/EngineConfig.hpp"
#include "Engine/Audio/AudioSystem.hpp"

#include "Engine/Diagnostics.hpp"
#include "Core/Logger.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <filesystem>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#elif defined(__linux__)
#include <unistd.h>
#include <sys/resource.h>
#endif

namespace
{
    void logGlfwError(int errorCode, const char *description)
    {
        VX_DEV_ERROR_STREAM("GLFW error (" << errorCode << "): " << (description ? description : "Unknown error") << '\n');
    }

    inline void tuneProcessFileDescriptorLimits()
    {
#if defined(_WIN32)
        constexpr int desiredMaxStdio = 2048;
        const int configuredLimit = _setmaxstdio(desiredMaxStdio);
        if (configuredLimit < desiredMaxStdio)
            VX_ENGINE_WARNING_STREAM("Failed to raise stdio descriptor limit to " << desiredMaxStdio << ". Current limit: " << configuredLimit << '\n');
#elif defined(__linux__)
        rlimit limits{};
        if (getrlimit(RLIMIT_NOFILE, &limits) != 0)
            return;

        const rlim_t targetLimit = std::min<rlim_t>(limits.rlim_max, static_cast<rlim_t>(8192));
        if (limits.rlim_cur >= targetLimit)
            return;

        rlimit updated = limits;
        updated.rlim_cur = targetLimit;
        if (setrlimit(RLIMIT_NOFILE, &updated) != 0)
            VX_ENGINE_WARNING_STREAM("Failed to raise process file descriptor limit (current: " << limits.rlim_cur << ", max: " << limits.rlim_max << ")\n");
#endif
    }

}

ELIX_NESTED_NAMESPACE_BEGIN(engine)

int ApplicationLoop::run(const ApplicationConfig &applicationConfig, const RuntimeFactory &runtimeFactory)
{
    {
        ScopedTimer timer("Core init");
        if (!preInit(applicationConfig))
            return 1;
    }

    if (!runtimeFactory)
    {
        VX_DEV_ERROR_STREAM("Failed to start: runtime factory is not set\n");
        shutdown();
        return 1;
    }

    m_runtime = runtimeFactory(applicationConfig);
    if (!m_runtime)
    {
        VX_DEV_ERROR_STREAM("Failed to start: runtime factory returned null runtime\n");
        shutdown();
        return 1;
    }

    {
        ScopedTimer timer("API init");
        if (!m_runtime->init())
        {
            VX_DEV_ERROR_STREAM("Runtime initialization failed\n");
            shutdown();
            return 1;
        }
    }

    VX_DEV_INFO_STREAM(
        "\n"
        "╔══════════════════════════════════════════════════════════════╗\n"
        "║                                                              ║\n"
        "║  ██╗   ██╗███████╗██╗     ██╗██╗  ██╗                        ║\n"
        "║  ██║   ██║██╔════╝██║     ██║╚██╗██╔╝                        ║\n"
        "║  ██║   ██║█████╗  ██║     ██║ ╚███╔╝                         ║\n"
        "║  ╚██╗ ██╔╝██╔══╝  ██║     ██║ ██╔██╗                         ║\n"
        "║   ╚████╔╝ ███████╗███████╗██║██╔╝ ██╗                        ║\n"
        "║    ╚═══╝  ╚══════╝╚══════╝╚═╝╚═╝  ╚═╝                        ║\n"
        "║                                                              ║\n"
        "║      Rendering your bad decisions in real time.              ║\n"
        "║      We are almost there...                                  ║\n"
        "║                                                              ║\n"
        "╚══════════════════════════════════════════════════════════════╝\n");

    loop();
    shutdown();

    return 0;
}

bool ApplicationLoop::preInit(const ApplicationConfig &applicationConfig)
{
    diagnostics::configureDefaultLogging();
    diagnostics::installCrashHandler();
    tuneProcessFileDescriptorLimits();

    auto &engineConfig = EngineConfig::instance();
    if (!engineConfig.reload())
        VX_ENGINE_WARNING_STREAM("Engine config loaded with errors: " << engineConfig.getConfigFilePath() << '\n');
    else
        VX_ENGINE_INFO_STREAM("Engine config: " << engineConfig.getConfigFilePath() << '\n');

    auto &renderSettings = RenderQualitySettings::getInstance();
    renderSettings.enableSSR = engineConfig.getSSREnabled();
    renderSettings.ssrMaxDistance = engineConfig.getSSRMaxDistance();
    renderSettings.ssrThickness = engineConfig.getSSRThickness();
    renderSettings.ssrStrength = engineConfig.getSSRStrength();
    renderSettings.ssrSteps = engineConfig.getSSRSteps();
    renderSettings.ssrRoughnessCutoff = engineConfig.getSSRRoughnessCutoff();

    glfwSetErrorCallback(logGlfwError);
    if (!glfwInit())
    {
        const char *description = nullptr;
        const int errorCode = glfwGetError(&description);
        if (errorCode != GLFW_NO_ERROR && description && description[0])
            VX_DEV_ERROR_STREAM("Failed to initialize GLFW: " << description << " (" << errorCode << ")\n");
        else
            VX_DEV_ERROR_STREAM("Failed to initialize GLFW\n");
        return false;
    }
    // TODO ONLY IF DEBUG
    const auto repoRootFromBuild = diagnostics::getExecutableDirectory().parent_path();
    if (!repoRootFromBuild.empty() && std::filesystem::exists(repoRootFromBuild / "resources"))
        std::filesystem::current_path(repoRootFromBuild);
    //

    if (!PhysXCore::init())
    {
        VX_ENGINE_ERROR_STREAM("Failed to init physics");
        return false;
    }

    if (!audio::AudioSystem::init())
    {
        VX_ENGINE_ERROR_STREAM("Failed to init audio system");
        return false;
    }

    AssetsLoader::clearAssetLoaders();
    AssetsLoader::registerAssetLoader(std::make_shared<OBJAssetLoader>());
    AssetsLoader::registerAssetLoader(std::make_shared<FBXAssetLoader>());
    AssetsLoader::registerAssetLoader(std::make_shared<MaterialAssetLoader>());
    AssetsLoader::registerAssetLoader(std::make_shared<TerrainAssetLoader>());

    m_window = platform::Window::create(800, 600, "Velix", platform::Window::WindowFlags::EWINDOW_FLAGS_DEFAULT);
    m_window->centerizedOnScreen();
    scripting::setActiveWindow(m_window.get());

    m_vulkanContext = core::VulkanContext::create(*m_window);

#ifdef _WIN32
    // On Windows each DLL has its own volk global function-pointer table.
    // Core initialised its copy inside VulkanContext::create(); now populate
    // the Engine DLL's copy with the same instance/device.
    volkInitialize();
    volkLoadInstance(m_vulkanContext->getInstance());
    volkLoadDevice(m_vulkanContext->getDevice()->vk());
#endif

    auto props = m_vulkanContext->getPhysicalDevicePoperties();

    const auto executableDirectory = diagnostics::getExecutableDirectory();
    const std::string pipelineCacheFileName = "pipeline_cache_" + std::string(props.deviceName) + "_" + std::to_string(props.vendorID) + '_' +
                                              std::to_string(props.driverVersion) + ".elixgpbin";
    m_graphicsPipelineCachePath = executableDirectory.empty()
                                  ? pipelineCacheFileName
                                  : (executableDirectory / pipelineCacheFileName).string();

    cache::GraphicsPipelineCache::loadCacheFromFile(m_vulkanContext->getDevice(), m_graphicsPipelineCachePath);
    EngineShaderFamilies::initEngineShaderFamilies();
    GraphicsPipelineManager::init();

    elix::engine::Texture::createDefaults();
    elix::engine::Material::createDefaultMaterial(elix::engine::Texture::getDefaultWhiteTexture());

    return true;
}

void ApplicationLoop::loop()
{
    if (!m_runtime)
        return;

    float lastFrame = 0.0f;
    float deltaTime = 0.0f;
    float lastCacheSave = 0.0f;
    constexpr float kCacheSaveInterval = 30.0f;

    while (m_window->isOpen())
    {
        const float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        m_window->pollEvents();
        scripting::beginFrame(deltaTime);

        m_runtime->tick(deltaTime);

        if (currentFrame - lastCacheSave >= kCacheSaveInterval)
        {
            cache::GraphicsPipelineCache::saveCacheToFile(m_vulkanContext->getDevice(), m_graphicsPipelineCachePath);
            lastCacheSave = currentFrame;
        }
    }
}

void ApplicationLoop::shutdown()
{
    if (m_runtime)
    {
        m_runtime->shutdown();
        m_runtime.reset();
    }

    ReflectionProbeComponent::flushDeferredCapturedCubemapReleases();

    glfwTerminate();

    PhysXCore::shutdown();
    audio::AudioSystem::shutdown();

    cache::GraphicsPipelineCache::saveCacheToFile(m_vulkanContext->getDevice(), m_graphicsPipelineCachePath);
    GraphicsPipelineManager::destroy();
    cache::GraphicsPipelineCache::deleteCache(m_vulkanContext->getDevice());

    scripting::setActiveScene(nullptr);
    scripting::setActiveWindow(nullptr);
    Material::deleteDefaultMaterial();
    Texture::destroyDefaults();

    utilities::AsyncGpuUpload::shutdown(m_vulkanContext->getDevice());

    AssetsLoader::clearAssetLoaders();

    EngineShaderFamilies::cleanEngineShaderFamilies();

    m_vulkanContext->cleanup();
}

ELIX_NESTED_NAMESPACE_END
