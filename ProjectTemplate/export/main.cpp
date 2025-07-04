#include <ElixirCore/Application.hpp>
#include <ElixirCore/ShaderManager.hpp>
#include <ElixirCore/DefaultRender.hpp>

int main()
{
    auto application = elix::Application::createApplication();

	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(application->getWindow()->getOpenGLWindow(), &bufferWidth, &bufferHeight);

	window::Window::setViewport(0, 0, bufferWidth, bufferHeight);

    auto fbo = application->getRenderer()->initFbo(bufferWidth, bufferHeight);

    auto defaultRender = application->getRenderer()->addRenderPath<elix::DefaultRender>();

    ShaderManager::instance().preLoadShaders();

    while (application->getWindow()->isWindowOpened())
    {
        application->update();

        application->render();

        application->endRender();
    }

    elix::Application::shutdownCore();

    return EXIT_SUCCESS;
}