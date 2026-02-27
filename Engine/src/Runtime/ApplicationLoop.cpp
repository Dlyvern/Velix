#include "Engine/Runtime/ApplicationLoop.hpp"

#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Assets/OBJAssetLoader.hpp"
#include "Engine/Assets/FBXAssetLoader.hpp"
#include "Engine/Assets/MaterialAssetLoader.hpp"
#include "Engine/Physics/PhysXCore.hpp"
#include "Engine/Scripting/VelixAPI.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Caches/GraphicsPipelineCache.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Scoped/ScopedTimer.hpp"
#include "Engine/Utilities/AsyncGpuUpload.hpp"
#include "Engine/Runtime/EngineConfig.hpp"
#include "Engine/Audio/AudioSystem.hpp"

#include "Core/Logger.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#elif defined(__linux__)
#include <unistd.h>
#include <sys/resource.h>
#endif

namespace
{
    // TODO remove this shit
    inline std::filesystem::path getExecutablePath()
    {
#if defined(_WIN32)
        char buffer[MAX_PATH];
        DWORD size = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
        if (size == 0 || size == MAX_PATH)
            return {};
        return std::filesystem::path(buffer).parent_path();

#elif defined(__linux__)
        char buffer[1024];
        ssize_t size = readlink("/proc/self/exe", buffer, sizeof(buffer));
        if (size <= 0 || size >= static_cast<ssize_t>(sizeof(buffer)))
            return {};
        return std::filesystem::path(std::string(buffer, size)).parent_path();
#else
        return {};
#endif
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
            return 0;
    }

    if (!runtimeFactory)
    {
        VX_DEV_ERROR_STREAM("Failed to start: runtime factory is not set\n");
        shutdown();
        return 0;
    }

    m_runtime = runtimeFactory(applicationConfig);
    if (!m_runtime)
    {
        VX_DEV_ERROR_STREAM("Failed to start: runtime factory returned null runtime\n");
        shutdown();
        return 0;
    }

    {
        ScopedTimer timer("API init");
        if (!m_runtime->init())
        {
            VX_DEV_ERROR_STREAM("Runtime initialization failed\n");
            shutdown();
            return 0;
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

    return 1;
}

bool ApplicationLoop::preInit(const ApplicationConfig &applicationConfig)
{
    core::Logger::createDefaultLogger();
    tuneProcessFileDescriptorLimits();

    auto &engineConfig = EngineConfig::instance();
    if (!engineConfig.reload())
        VX_ENGINE_WARNING_STREAM("Engine config loaded with errors: " << engineConfig.getConfigFilePath() << '\n');
    else
        VX_ENGINE_INFO_STREAM("Engine config: " << engineConfig.getConfigFilePath() << '\n');

    if (!glfwInit())
    {
        VX_DEV_ERROR_STREAM("Failed to initialize GLFW\n");
        return false;
    }
    // TODO ONLY IF DEBUG
    const auto repoRootFromBuild = getExecutablePath().parent_path();
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

    m_window = platform::Window::create(800, 600, "Velix", platform::Window::WindowFlags::EWINDOW_FLAGS_DEFAULT);
    scripting::setActiveWindow(m_window.get());

    m_vulkanContext = core::VulkanContext::create(*m_window);
    auto props = m_vulkanContext->getPhysicalDevicePoperties();

    m_graphicsPipelineCachePath = getExecutablePath().string() +
                                  "/pipeline_cache_" + std::string(props.deviceName) + "_" + std::to_string(props.vendorID) + '_' +
                                  std::to_string(props.driverVersion) + ".elixgpbin";

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

    while (m_window->isOpen())
    {
        const float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        m_window->pollEvents();

        m_runtime->tick(deltaTime);
    }
}

void ApplicationLoop::shutdown()
{
    if (m_runtime)
    {
        m_runtime->shutdown();
        m_runtime.reset();
    }

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

    utilities::AsyncGpuUpload::flush(m_vulkanContext->getDevice());

    AssetsLoader::clearAssetLoaders();

    m_vulkanContext->cleanup();
}

ELIX_NESTED_NAMESPACE_END
