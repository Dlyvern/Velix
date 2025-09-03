#include <VelixFlow/Application.hpp>
#include <VelixFlow/ShaderManager.hpp>
#include <VelixFlow/AssetsCache.hpp>
#include <VelixFlow/BinarySerializer.hpp>
#include <VelixFlow/Assets.hpp>
#include <VelixFlow/Filesystem.hpp>
#include <VelixFlow/RenderAPI/OpenGL/GLShadowRender.hpp>
#include <VelixFlow/RenderAPI/OpenGL/GLSceneRender.hpp>
#include <VelixFlow/Scripting/ScriptSystem.hpp>

#include <vector>
#include <memory>

int main()
{
    auto application = elix::Application::createApplication();

	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(application->getWindow()->getGLFWWindow(), &bufferWidth, &bufferHeight);

	window::Window::setViewport(0, 0, bufferWidth, bufferHeight);

    auto defaultRender = application->getRenderer()->addRenderPath<elix::render::GLSceneRender>();

    defaultRender->setWindow(application->getWindow());

    elix::BinarySerializer serializer;

    std::vector<std::unique_ptr<elix::AssetModel>> outModels;

    serializer.readElixPacket("assets.elixpacket", outModels);

    elix::AssetsCache assetsCache; 

    for(auto& model : outModels)
    {
        const std::string modelName = model->getModel()->getName();
        
        assetsCache.addAsset<elix::AssetModel>(modelName, std::move(model));
    }

    if(!elix::scripting::ScriptSystem::loadLibrary(elix::filesystem::getExecutablePath().string() + "/libGameLib.so"))
        ELIX_LOG_ERROR("Failed load library");

    application->getScene()->loadSceneFromFile(elix::filesystem::getExecutablePath().string() + "/default_scene.scene", assetsCache);

    application->getRenderer()->addRenderPath<elix::render::GLShadowRender>(application->getScene()->getLights());

    ShaderManager::instance().preLoadShaders();

    while (application->getWindow()->isWindowOpened())
    {
        application->update();

        application->render();

        application->endRender();
    }

    application->shutdownCore();

    return EXIT_SUCCESS;
}