#include "Renderer.hpp"

#include <glad.h>
#include <glm/ext/matrix_clip_space.hpp>
#include "CameraManager.hpp"
#include "ElixirCore/LightManager.hpp"
#include "ElixirCore/SceneManager.hpp"
#include "ElixirCore/ShaderManager.hpp"
#include "ElixirCore/SkeletalMeshComponent.hpp"
#include "ElixirCore/StaticMeshComponent.hpp"
#include "ElixirCore/WindowsManager.hpp"
#include <iostream>

#include "Editor.hpp"

Renderer& Renderer::instance()
{
    static Renderer self;
    return self;
}

void Renderer::initFrameBuffer(int width, int height)
{
    // auto window = window::WindowsManager::instance().getCurrentWindow();

    // m_width = window->getWidth();
    // m_height = window->getHeight();

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_colorTexture);
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);

    glGenRenderbuffers(1, &m_depthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthBuffer);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "Framebuffer not complete!" << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    m_isFrameBufferInitialized = true;
}

void Renderer::rescaleBuffer(float width, float height)
{
    // m_width = width;
    // m_height = height;
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);

    glBindRenderbuffer(GL_RENDERBUFFER, m_depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthBuffer);
}

void Renderer::bindBuffer()
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
}

void Renderer::unbindBuffer()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::beginFrame()
{
    // if (!m_isFrameBufferInitialized)
    //     initFrameBuffer();
    //
    // glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    // glViewport(0, 0, m_width, m_height);
    // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // if (DebugEditor::instance().getDebugMode())
    // {
    //     bindBuffer();
    //     glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // }

    bindBuffer();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const auto* currentWindow = window::WindowsManager::instance().getCurrentWindow();
    const float aspect = static_cast<float>(currentWindow->getWidth()) / static_cast<float>(currentWindow->getHeight());

    m_frameData.projectionMatrix = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);

    const auto* activeCamera = CameraManager::getInstance().getActiveCamera();
    m_frameData.viewMatrix = activeCamera->getViewMatrix();
    m_frameData.cameraPosition = activeCamera->getPosition();

    //TODO: These two lines are breaking everything
    // updateLightSpaceMatrix();
    // renderShadowPass(SceneManager::instance().getCurrentScene()->getGameObjects());

    // if (!DebugEditor::instance().getDebugMode())
    // {
        updateLightSpaceMatrix();
        renderShadowPass(SceneManager::instance().getCurrentScene()->getGameObjects());
    // }

    const auto skeletonShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::SKELETON);
    const auto staticShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::STATIC);
    GLitch::Shader* shader{nullptr};

    const auto& gameObjects = SceneManager::instance().getCurrentScene()->getGameObjects();

    for (const auto& gameObject : gameObjects)
    {
        if (gameObject->hasComponent<SkeletalMeshComponent>())
            shader = skeletonShader;
        else if (gameObject->hasComponent<StaticMeshComponent>())
            shader = staticShader;

        if (!shader)
            continue;

        shader->bind();

        shader->setMat4("model", gameObject->getTransformMatrix());
        shader->setMat4("view", m_frameData.viewMatrix);
        shader->setMat4("projection", m_frameData.projectionMatrix);
        shader->setVec3("viewPos", m_frameData.cameraPosition);

        if (gameObject->hasComponent<StaticMeshComponent>())
            gameObject->getComponent<StaticMeshComponent>()->render(*shader, gameObject->getTransformMatrix(), &gameObject->overrideMaterials);
        else if (gameObject->hasComponent<SkeletalMeshComponent>())
        {
            gameObject->getComponent<SkeletalMeshComponent>()->render(*shader, gameObject->getTransformMatrix(), &gameObject->overrideMaterials);
            LightManager::instance().bindGlobalLighting(*shader);
        }

        shader = nullptr;
    }

    LightManager::instance().bindGlobalLighting(*skeletonShader);
    LightManager::instance().bindGlobalLighting(*staticShader);

    m_shadowHandler.activateTexture();
}

void Renderer::endFrame()
{
    m_shadowHandler.deactivateTexture();
    unbindBuffer();
    // if (DebugEditor::instance().getDebugMode())
        // unbindBuffer();
    // glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

const RendererFrameData& Renderer::getFrameData() const
{
    return m_frameData;
}

void Renderer::initShadows()
{
    m_shadowHandler.initShadows();
}

void Renderer::updateLightSpaceMatrix()
{
    static constexpr float nearPlane = 1.0f, farPlane = 40.0f;

    if (!LightManager::instance().getDirectionalLight())
        return;

    glm::vec3 lightDir = glm::normalize(LightManager::instance().getDirectionalLight()->direction);
    glm::vec3 lightTarget{0.0f};
    glm::vec3 lightPos = lightTarget - lightDir * 20.0f;

    glm::mat4 lightView = glm::lookAt(lightPos, lightTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 lightProjection = glm::ortho(-30.0f, 30.0f, -30.0f, 30.0f, nearPlane, farPlane);

    glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    LightManager::instance().setLightSpaceMatrix(lightSpaceMatrix);
}

void Renderer::renderShadowPass(const std::vector<std::shared_ptr<GameObject>> &gameObjects)
{
    auto* staticShadowShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::STATIC_SHADOW);
    auto* skeletonShadowShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::SKELETON_SHADOW);

    m_shadowHandler.bindShadows();

    for (auto* shader : { staticShadowShader, skeletonShadowShader })
    {
        shader->bind();
        shader->setMat4("lightSpaceMatrix", LightManager::instance().getLightSpaceMatrix());

        for (const auto& obj : gameObjects)
        {
            if (obj->hasComponent<StaticMeshComponent>())
                obj->getComponent<StaticMeshComponent>()->render(*shader, obj->getTransformMatrix());
            else if (obj->hasComponent<SkeletalMeshComponent>())
                obj->getComponent<SkeletalMeshComponent>()->render(*shader, obj->getTransformMatrix());
        }
    }

    m_shadowHandler.unbindShadows();
}
