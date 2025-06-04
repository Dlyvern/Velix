#include "Renderer.hpp"

#include <glad/glad.h>
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
    // elix::Texture::TextureParams params;
    //
    // params.width = width;
    // params.height = height;
    // params.format = elix::Texture::TextureFormat::RGB;
    // params.usage = elix::Texture::TextureUsage::RenderTarget;
    // params.generateMipmaps = false;
    //
    // m_colorTexture.loadEmpty(&params);

    m_width = width;
    m_height = height;

    glGenTextures(1, &m_colorTexture);
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);

    glGenRenderbuffers(1, &m_depthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthBuffer);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "Framebuffer not complete!" << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void Renderer::rescaleBuffer(float width, float height)
{
    if ((int)m_width != (int)width || (int)m_height != (int)height)
    {
        m_width = width;
        m_height = height;

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        glBindTexture(GL_TEXTURE_2D, m_colorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);

        glBindRenderbuffer(GL_RENDERBUFFER, m_depthBuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthBuffer);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }
}

void Renderer::bindBuffer()
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
}

void Renderer::unbindBuffer()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::updateFrameData()
{
    const auto* currentWindow = window::WindowsManager::instance().getCurrentWindow();

    // const float aspect = static_cast<float>(currentWindow->getWidth()) / static_cast<float>(currentWindow->getHeight());
    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);

    m_frameData.projectionMatrix = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);

    const auto* activeCamera = CameraManager::getInstance().getActiveCamera();
    m_frameData.viewMatrix = activeCamera->getViewMatrix();
    m_frameData.cameraPosition = activeCamera->getPosition();
}

// glGetIntegerv(GL_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&previousFBO));
// std::cout << previousFBO << std::endl;

void Renderer::beginFrame()
{
    if (!SceneManager::instance().getCurrentScene())
        return;

    bindBuffer();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    updateFrameData();

    updateLightSpaceMatrix();
    // renderShadowPass(SceneManager::instance().getCurrentScene()->getGameObjects());

    const auto& gameObjects = SceneManager::instance().getCurrentScene()->getGameObjects();
    const auto& drawables = SceneManager::instance().getCurrentScene()->getDrawables();

    elix::Shader* skeletonShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::SKELETON);
    elix::Shader* staticShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::STATIC);

    staticShader->bind();
    staticShader->setMat4("view", m_frameData.viewMatrix);
    staticShader->setMat4("projection", m_frameData.projectionMatrix);
    staticShader->setVec3("viewPos", m_frameData.cameraPosition);
    LightManager::instance().setLightSpaceMatricesInShader(*staticShader);
    LightManager::instance().sendLightsIntoShader(*staticShader);
    LightManager::instance().bindSpotLighting(*staticShader);
    LightManager::instance().bindGlobalLighting(*staticShader);
    LightManager::instance().bindPointLighting(*staticShader);
    staticShader->unbind();

    skeletonShader->bind();
    skeletonShader->setMat4("view", m_frameData.viewMatrix);
    skeletonShader->setMat4("projection", m_frameData.projectionMatrix);
    skeletonShader->setVec3("viewPos", m_frameData.cameraPosition);
    LightManager::instance().setLightSpaceMatricesInShader(*skeletonShader);
    LightManager::instance().sendLightsIntoShader(*skeletonShader);
    LightManager::instance().bindSpotLighting(*skeletonShader);
    LightManager::instance().bindGlobalLighting(*skeletonShader);
    LightManager::instance().bindPointLighting(*skeletonShader);
    skeletonShader->unbind();

    for (const auto& gameObject : gameObjects)
    {
        if (gameObject->hasComponent<StaticMeshComponent>())
        {
            staticShader->bind();
            staticShader->setMat4("model", gameObject->getTransformMatrix());
            gameObject->getComponent<StaticMeshComponent>()->render(&gameObject->overrideMaterials);
            staticShader->unbind();
        }
        else if (gameObject->hasComponent<SkeletalMeshComponent>())
        {
            skeletonShader->bind();
            skeletonShader->setMat4("model", gameObject->getTransformMatrix());
            gameObject->getComponent<SkeletalMeshComponent>()->render(*skeletonShader, &gameObject->overrideMaterials);
            skeletonShader->unbind();
        }
    }

    for (const auto& drawable : drawables)
        drawable->draw();

    if (SceneManager::instance().getCurrentScene()->getSkybox())
        SceneManager::instance().getCurrentScene()->getSkybox()->render(m_frameData.viewMatrix, m_frameData.projectionMatrix);

    // glm::vec3 start = glm::vec3(0.0f, 0.0f, 0.0f);
    // glm::vec3 end = glm::vec3(1.0f, 1.0f, 1.0f);
    //
    // debugLine.draw(start, end, m_frameData.viewMatrix, m_frameData.projectionMatrix);

    m_shadowHandler.bindDirectionalShadowPass(8);

    const auto& spotLight = LightManager::instance().getSpotLights();

    for (size_t index = 0; index < spotLight.size(); ++index)
        m_shadowHandler.bindSpotShadowPass(index, 10 + static_cast<int>(index));

}

void Renderer::updateLightSpaceMatrix()
{
    if (LightManager::instance().getLights().empty())
        return;

    static constexpr float directionalNearPlane = 1.0f, directionalFarPlane = 40.0f;
    static constexpr float spotNearPlane = 0.1f, spotFarPlane = 100.0f;
    static constexpr float pointNearPlane = 0.1f, pointFarPlane = 25.0f;

    std::vector<glm::mat4> lightSpaceMatrices;

    if (auto* dirLight = LightManager::instance().getDirectionalLight()) {
        glm::vec3 lightDir = glm::normalize(dirLight->direction);
        glm::vec3 lightTarget{0.0f};
        glm::vec3 lightPos = lightTarget - lightDir * 20.0f;

        glm::mat4 lightView = glm::lookAt(lightPos, lightTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightProjection = glm::ortho(-30.0f, 30.0f, -30.0f, 30.0f,
                                             directionalNearPlane, directionalFarPlane);

        lightSpaceMatrices.push_back(lightProjection * lightView);
    }

    for (const auto& light : LightManager::instance().getSpotLights())
    {
        float aspectRatio = 1.0f;
        float fov = glm::radians(light->outerCutoff * 2.0f);

        glm::mat4 lightProjection = glm::perspective(fov, aspectRatio, spotNearPlane, spotFarPlane);

        glm::vec3 up = glm::abs(glm::dot(glm::normalize(light->direction), glm::vec3(0, 1, 0))) > 0.99f
             ? glm::vec3(0, 0, 1)
             : glm::vec3(0, 1, 0);

        glm::mat4 lightView = glm::lookAt(light->position,
                                        light->position + light->direction,
                                        up);

        lightSpaceMatrices.push_back(lightProjection * lightView);
    }

    LightManager::instance().setLightSpaceMatrix(lightSpaceMatrices);
}

void Renderer::renderShadowPass(const std::vector<std::shared_ptr<GameObject>> &gameObjects)
{
    if (LightManager::instance().getLights().empty() || LightManager::instance().getLightSpaceMatrix().empty())
        return;

    auto* staticShadowShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::STATIC_SHADOW);
    auto* skeletonShadowShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::SKELETON_SHADOW);

    const auto& lightSpaceMatrices = LightManager::instance().getLightSpaceMatrix();
    const auto& lights = LightManager::instance().getLights();

    for (int i = 0; i < lights.size(); ++i)
    {
        const auto& lightType = lights[i]->type;

        if (lightType == lighting::LightType::DIRECTIONAL)
            m_shadowHandler.beginDirectionalShadowPass();
        else if (lightType == lighting::LightType::SPOT)
            m_shadowHandler.beginSpotShadowPass(i);
        else if (lightType == lighting::LightType::POINT)
            m_shadowHandler.beginPointShadowPass(i);

        for (const auto& obj : gameObjects)
        {
            if (obj->hasComponent<StaticMeshComponent>())
            {
                staticShadowShader->bind();
                staticShadowShader->setMat4("model", obj->getTransformMatrix());
                staticShadowShader->setMat4("lightSpaceMatrix", lightSpaceMatrices[i]);
                obj->getComponent<StaticMeshComponent>()->render();
                staticShadowShader->unbind();
            }
            else if (obj->hasComponent<SkeletalMeshComponent>())
            {
                skeletonShadowShader->bind();
                skeletonShadowShader->setMat4("model", obj->getTransformMatrix());
                skeletonShadowShader->setMat4("lightSpaceMatrix", lightSpaceMatrices[i]);
                obj->getComponent<SkeletalMeshComponent>()->render(*skeletonShadowShader);
                skeletonShadowShader->unbind();
            }
        }

        m_shadowHandler.endShadowPass();
    }
}

void Renderer::endFrame()
{
    unbindBuffer();
}

const RendererFrameData& Renderer::getFrameData() const
{
    return m_frameData;
}

void Renderer::initShadows()
{
    m_shadowHandler.initAllShadows();
}
