#include <glad/glad.h>

#include "Engine.hpp"
#include "StencilRender.hpp"

#include "VelixFlow/ShaderManager.hpp"
#include "VelixFlow/Logger.hpp"
#include <VelixFlow/DefaultRender.hpp>

#include <VelixFlow/Filesystem.hpp>
#include <VelixFlow/AssetsLoader.hpp>
#include <VelixFlow/ShaderManager.hpp>

void RenderQuad()
{
    static unsigned int quadVAO = 0;
    static unsigned int quadVBO;

    if (quadVAO == 0)
    {
        float quadVertices[] =
        {
            -0.5f,  0.5f, 0.0f,  0.0f, 1.0f,
            -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
             0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
            -0.5f,  0.5f, 0.0f,  0.0f, 1.0f,
             0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
             0.5f,  0.5f, 0.0f,  1.0f, 1.0f
        };

        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);

        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    }

    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void RenderBillboardIcon(glm::vec3 position, elix::Texture* icon, glm::mat4 view, glm::mat4 projection, elix::Shader& shader)
{
    glm::vec3 camRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
    glm::vec3 camUp = glm::vec3(view[0][1], view[1][1], view[2][1]);

    float scale = 0.5f;
    glm::mat4 model(1.0f);
    model[0] = glm::vec4(camRight * scale, 0.0f);
    model[1] = glm::vec4(camUp * scale, 0.0f);
    model[2] = glm::vec4(glm::cross(camRight, camUp), 0.0f);
    model[3] = glm::vec4(position, 1.0f);

    glm::mat4 mvp = projection * view * model;

    shader.bind();
    shader.setMat4("uMVP", mvp);
    icon->bind(0);
    shader.setInt("uTexture", 0);

    RenderQuad();
}

int Engine::run()
{
    try
    {
        init();
    }
    catch (const std::exception &e)
    {
        ELIX_LOG_ERROR("COULD NOT INITIALIZE ENGINE: ",e.what());
        return EXIT_FAILURE;
    }

    auto textureAsset = elix::AssetsLoader::loadAsset(elix::filesystem::getExecutablePath().string() + "/resources/textures/folder.png");
    auto texture = dynamic_cast<elix::AssetTexture*>(textureAsset.get())->getTexture();

    while (s_application->getWindow()->isWindowOpened())
    {
        s_application->update();

        // GLuint queryID;

        // glGenQueries(1, &queryID);

        // glBeginQuery(GL_TIME_ELAPSED, queryID);

        s_application->render();

    	s_editor->update(s_application->getDeltaTime());

        // RenderBillboardIcon({0.0, 0.0f, 0.0f}, texture, s_application->getCamera()->getViewMatrix(),
        // s_application->getCamera()->getProjectionMatrix(), *ShaderManager::instance().getShader(ShaderManager::ShaderType::BILLBOARD));

        s_application->endRender();

        // glEndQuery(GL_TIME_ELAPSED);

        // GLuint64 elapsedTime;
        // glGetQueryObjectui64v(queryID, GL_QUERY_RESULT, &elapsedTime);

        // std::cout << "GPU Time: " << elapsedTime / 1e6 << " ms" << std::endl;
    }

    s_editor->destroy();

    elix::Application::shutdownCore();

    return EXIT_SUCCESS;
}

void Engine::init()
{
    s_application = elix::Application::createApplication();
    s_editor = std::make_unique<Editor>();

    s_editor->init();
	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(s_application->getWindow()->getOpenGLWindow(), &bufferWidth, &bufferHeight);

	window::Window::setViewport(0, 0, bufferWidth, bufferHeight);

    auto fbo = s_application->getRenderer()->initFbo(bufferWidth, bufferHeight);

    auto defaultRender = s_application->getRenderer()->addRenderPath<elix::DefaultRender>();
    defaultRender->setRenderTarget(fbo);

    auto stencilRender = s_application->getRenderer()->addRenderPath<StencilRender>();
    stencilRender->setRenderTarget(fbo);

    ShaderManager::instance().preLoadShaders();

    auto data = elix::Texture::loadImage(elix::filesystem::getExecutablePath().string() + "/resources/textures/ElixirLogo.png", false);
    data.numberOfChannels = 4;

    if(data.data)
    {
        GLFWimage images[1];
        images[0].height = data.height;
        images[0].width = data.width;
        images[0].pixels = data.data;

        glfwSetWindowIcon(s_application->getWindow()->getOpenGLWindow(), 1, images);
    }
    else
        ELIX_LOG_ERROR("Failed to load logo");
}

