#include <glad/glad.h>

#include "Renderer.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include "CameraManager.hpp"
#include "ElixirCore/LightManager.hpp"
#include "ElixirCore/SceneManager.hpp"
#include "ElixirCore/ShaderManager.hpp"
#include "ElixirCore/SkeletalMeshComponent.hpp"
#include "ElixirCore/StaticMeshComponent.hpp"
#include <ElixirCore/MeshComponent.hpp>
#include "ElixirCore/WindowsManager.hpp"
#include <iostream>
#include <ElixirCore/LightComponent.hpp>

#include "Editor.hpp"
#include <glm/gtx/string_cast.hpp>

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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);

    glGenRenderbuffers(1, &m_depthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthBuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthBuffer);

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
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);

        glBindRenderbuffer(GL_RENDERBUFFER, m_depthBuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthBuffer);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthBuffer);

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
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT  | GL_STENCIL_BUFFER_BIT);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    updateFrameData();

    updateLightSpaceMatrix();
    // renderShadowPass(SceneManager::instance().getCurrentScene()->getGameObjects());

    const auto& gameObjects = SceneManager::instance().getCurrentScene()->getGameObjects();
    const auto& drawables = SceneManager::instance().getCurrentScene()->getDrawables();

    elix::Shader* skeletonShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::SKELETON);
    elix::Shader* staticShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::STATIC);

    elix::Shader* staticStencilShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::STATIC_STENCIL);
    elix::Shader* skeletonStencilShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::SKELETON_STENCIL);

    staticStencilShader->bind();
    staticStencilShader->setMat4("view", m_frameData.viewMatrix);
    staticStencilShader->setMat4("projection", m_frameData.projectionMatrix);
    staticStencilShader->setVec3("viewPos", m_frameData.cameraPosition);
    staticStencilShader->unbind();

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

    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilMask(0xFF);

    for (const auto& gameObject : gameObjects)
    {
        bool isSelected = (gameObject.get() == m_selectedGameObject);

        if (gameObject->hasComponent<MeshComponent>())
        {
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glStencilMask(isSelected ? 0xFF : 0x00);

            auto* justShader = gameObject->getComponent<MeshComponent>()->getModel()->hasSkeleton() ? skeletonShader : staticShader;

            justShader->bind();
            justShader->setMat4("model", gameObject->getTransformMatrix());

            if (justShader == skeletonShader)
            {
                const std::vector<glm::mat4>& boneMatrices = gameObject->getComponent<MeshComponent>()->getModel()->getSkeleton()->getFinalMatrices();
                justShader->setMat4Array("finalBonesMatrices", boneMatrices);
            }

            gameObject->getComponent<MeshComponent>()->render(&gameObject->overrideMaterials);
            justShader->unbind();
        }
    }

    if (SceneManager::instance().getCurrentScene()->getSkybox())
        SceneManager::instance().getCurrentScene()->getSkybox()->render(m_frameData.viewMatrix, m_frameData.projectionMatrix);

    for (const auto& drawable : drawables)
        drawable->draw();

    if (m_selectedGameObject && m_selectedGameObject->hasComponent<MeshComponent>())
    {
        glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        glStencilMask(0x00);
        glDisable(GL_DEPTH_TEST);

        glm::mat4 model = glm::scale(m_selectedGameObject->getTransformMatrix(), glm::vec3(1.05f));

        const auto* justShader = m_selectedGameObject->getComponent<MeshComponent>()->getModel()->hasSkeleton() ? skeletonStencilShader : staticStencilShader;

        justShader->bind();
        justShader->setMat4("model", model);
        m_selectedGameObject->getComponent<MeshComponent>()->render();
        justShader->unbind();

        glEnable(GL_DEPTH_TEST);
        glStencilMask(0xFF);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
    }

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

void Renderer::setSelectedGameObject(GameObject *gameObject)
{
    m_selectedGameObject = gameObject;
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
            // if (obj->hasComponent<StaticMeshComponent>())
            // {
            //     staticShadowShader->bind();
            //     staticShadowShader->setMat4("model", obj->getTransformMatrix());
            //     staticShadowShader->setMat4("lightSpaceMatrix", lightSpaceMatrices[i]);
            //     obj->getComponent<StaticMeshComponent>()->render();
            //     staticShadowShader->unbind();
            // }
            // else if (obj->hasComponent<SkeletalMeshComponent>())
            // {
            //     skeletonShadowShader->bind();
            //     skeletonShadowShader->setMat4("model", obj->getTransformMatrix());
            //     skeletonShadowShader->setMat4("lightSpaceMatrix", lightSpaceMatrices[i]);
            //     obj->getComponent<SkeletalMeshComponent>()->render(*skeletonShadowShader);
            //     skeletonShadowShader->unbind();
            // }
        }

        m_shadowHandler.endShadowPass();
    }
}

void Renderer::endFrame()
{
    unbindBuffer();
    glDisable(GL_DEPTH_TEST);
}

const RendererFrameData& Renderer::getFrameData() const
{
    return m_frameData;
}

void Renderer::initShadows()
{
    m_shadowHandler.initAllShadows();
}
