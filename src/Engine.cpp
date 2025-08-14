#include "Engine.hpp"
#include "StencilRender.hpp"
#include "VelixFlow/Logger.hpp"
#include <VelixFlow/Filesystem.hpp>
#include <VelixFlow/AssetsLoader.hpp>
#include <VelixFlow/UI/UIVerticalBox.hpp>
#include <VelixFlow/UI/UIText.hpp>
#include <VelixFlow/UI/UIButton.hpp>
#include <VelixFlow/MeshFactory.hpp>
#include <VelixFlow/TextureFactory.hpp>
#include <VelixFlow/Physics/Physics.hpp>
#include <VelixFlow/AudioSystem.hpp>
#include <VelixFlow/Scripting/LibrariesLoader.hpp>


#include "UIFadeAnimation.hpp"

#include <VelixFlow/Input/Keyboard.hpp>
#include <VelixFlow/Input/Mouse.hpp>

#include "ProjectManager.hpp"

int Engine::run()
{
    try
    {
        init();
    }
    catch (const std::exception &e)
    {
        ELIX_LOG_ERROR("FAILED TO INITIALIZE ENGINE: ", e.what());
        return EXIT_FAILURE;
    }

    auto project = new Project();

    const std::string path = "";

    if (!ProjectManager::instance().loadConfigInProject(path, project))
    {
        ELIX_LOG_ERROR("Failed to load project");
        delete project;
    }

    if (!ProjectManager::instance().loadProject(project))
    {
        ELIX_LOG_ERROR("Failed to load project");
        delete project;
    }

    ProjectManager::instance().setCurrentProject(project);

    s_scene->loadSceneFromFile(project->entryScene, *ProjectManager::instance().getAssetsCache());
    
    Camera camera(s_camera.get());

    float deltaTime{0.0f};
    std::chrono::high_resolution_clock::time_point lastTime = std::chrono::high_resolution_clock::now();

    s_renderer->addRenderPath("GLSceneRender", s_window, s_scene.get());
    s_renderer->addRenderPath("GLUIRender", s_window, s_scene.get());
    s_renderer->addRenderPath("GLShadowRender", s_window, s_scene.get());

    // UIFadeAnimation animation(velixTextLogo, 2.0f);

    while (s_window->isWindowOpened())
    {
        input::Mouse.update();
        
        auto now = std::chrono::high_resolution_clock::now();
        deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        s_renderer->pollEvents();

        physics::PhysicsController::instance().simulate(deltaTime);

        camera.update(deltaTime);
        s_scene->update(deltaTime);
        s_editor.update(deltaTime);
        // animation.update(deltaTime);

        const auto& frameData = s_renderer->updateFrameData(camera.getCamera(), s_window->getWidth(), s_window->getHeight());

        s_renderer->renderScene(frameData, s_scene.get());
        s_renderer->renderSceneWithPath(frameData, s_editor.getOverlay().get(), "GLUIRender");

        s_renderer->swapBuffers(s_window);
    }

    //TODO: make render API shutdown

    delete project;

    return EXIT_SUCCESS;
}

void Engine::initRenderAPI(elix::render::RenderAPI renderApi)
{
    std::string libName;

    s_selectedRenderAPI = renderApi;

    switch(renderApi)
    {
        case elix::render::RenderAPI::OpenGL:
            libName = "libVelixGL.so";
            ELIX_LOG_INFO("Using OpenGL render backend");
            break;
        case elix::render::RenderAPI::Vulkan:
            libName = "libVelixVK.so";
            ELIX_LOG_INFO("Using Vulkan render backend");
            break;
        default:
            throw std::runtime_error("Unknown render API");
    }

    std::string renderLibraryPath = "lib/" + libName;

    auto renderLibrary = elix::LibrariesLoader::loadLibrary(renderLibraryPath);

    if(!renderLibrary)
        throw std::runtime_error("Failed to load render library " + renderLibraryPath);

    auto getRendererFunction = (elix::IRenderer*(*)())elix::LibrariesLoader::getFunction("createRenderer", renderLibrary);

    if(!getRendererFunction)
        throw std::runtime_error("Failed to create renderer from library");
    
    s_renderer = getRendererFunction();

    s_renderContext = s_renderer->getContext();

    s_renderer->init(s_window);

    s_renderer->setKeyCallback(input::KeysManager::keyCallback, s_window);
    s_renderer->setMouseButtonCallback(input::MouseManager::mouseButtonCallback, s_window);
    s_renderer->setMousePositionCallback(input::MouseManager::mouseCallback, s_window);
}

void Engine::init()
{
    s_crashHandler.init();
    
    std::string api = "OpenGL";

    if(!s_engineConfig.load())
        ELIX_LOG_ERROR("Failed to load config");
    else
        api = s_engineConfig.getConfig().value("graphicsAPI", "OpenGL");

    if(api == "OpenGL")
        initRenderAPI(elix::render::RenderAPI::OpenGL);
    else if(api == "Vulkan")
        initRenderAPI(elix::render::RenderAPI::Vulkan);
    else    
        throw std::runtime_error("Unknown render API");

    s_scene = std::make_unique<elix::Scene>();
    s_camera = std::make_unique<elix::components::CameraComponent>();
    
    elix::mesh::MeshFactory::init(s_renderContext);
    elix::texture::TextureFactory::init(s_renderContext);

    physics::PhysicsController::instance().init();

    elix::audio::AudioSystem::instance().init();

    s_editor.init();
    
    // elix::Image image;

    // if(image.load(elix::filesystem::getExecutablePath().string() + "/resources/textures/velix_logo.png", false))
    // {
    //     s_application->getWindow()->setWindowIcon(image);
    //     image.free();
    // }
}

//INIT() = 	// int bufferWidth, bufferHeight;
	// glfwGetFramebufferSize(s_application->getWindow()->getGLFWWindow(), &bufferWidth, &bufferHeight);

	// window::Window::setViewport(0, 0, bufferWidth, bufferHeight);
    
    // auto fbo = s_application->getRenderer()->initFbo(bufferWidth, bufferHeight);

    // auto defaultRender = s_application->getRenderer()->addRenderPath<elix::render::GLSceneRender>();
    // auto stencilRender = s_application->getRenderer()->addRenderPath<StencilRender>();

    // defaultRender->setRenderTarget(fbo);

    // stencilRender->setRenderTarget(fbo);