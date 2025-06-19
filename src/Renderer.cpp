#include <ElixirCore/Light.hpp>
#include <glad/glad.h>

#include "Renderer.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include "CameraManager.hpp"
#include "ElixirCore/LightManager.hpp"
#include "ElixirCore/SceneManager.hpp"
#include "ElixirCore/ShaderManager.hpp"
#include <ElixirCore/MeshComponent.hpp>
#include <ElixirCore/LightComponent.hpp>

#include <glm/gtx/string_cast.hpp>
#include <string>


Renderer& Renderer::instance()
{
    static Renderer self;
    return self;
}

void Renderer::initFrameBuffer(int width, int height)
{
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
       ELIX_LOG_ERROR("Buffer is not completed");

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

unsigned int Renderer::getFrameBufferTexture() const
{
    return m_colorTexture;
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
	if(auto activeCamera = CameraManager::getInstance().getActiveCamera())
	{
        float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
        m_frameData.projectionMatrix = glm::perspective(glm::radians(activeCamera->getFOV()), aspect, activeCamera->getNear(), activeCamera->getFar());
		// m_frameData.projectionMatrix = activeCamera->getProjectionMatrix();
		m_frameData.viewMatrix = activeCamera->getViewMatrix();
		m_frameData.cameraPosition = activeCamera->getPosition();
	}
}


const elix::ShadowSystem::Shadow& Renderer::getShadowData(lighting::Light* light) const
{
    return m_shadowSystem.getShadowData(light);
}

//GL_TEXTURE_BINDING
// glGetIntegerv(GL_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&previousFBO));
// std::cout << previousFBO << std::endl;

void Renderer::beginFrame()
{
    if (!SceneManager::instance().getCurrentScene())
        return;

    updateFrameData();
    renderShadowPass(SceneManager::instance().getCurrentScene()->getGameObjects());

    bindBuffer();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT  | GL_STENCIL_BUFFER_BIT);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    glViewport(0, 0, m_width, m_height);

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

    skeletonStencilShader->bind();
    skeletonStencilShader->setMat4("view", m_frameData.viewMatrix);
    skeletonStencilShader->setMat4("projection", m_frameData.projectionMatrix);
    skeletonStencilShader->setVec3("viewPos", m_frameData.cameraPosition);
    skeletonStencilShader->unbind();

    staticShader->bind();
    staticShader->setMat4("view", m_frameData.viewMatrix);
    staticShader->setMat4("projection", m_frameData.projectionMatrix);
    staticShader->setVec3("viewPos", m_frameData.cameraPosition);
    LightManager::instance().sendLightsIntoShader(*staticShader);
    
    const auto& lights = LightManager::instance().getLights();

    for(size_t index = 0; index < lights.size(); ++index)
    {
        int textureSlot = 20 + index;
        auto* light = lights[index];
        const auto& lightMatrix = m_shadowSystem.getLightMatrix(light);
        staticShader->setInt("shadowMaps[" + std::to_string(index) + "]", textureSlot);
        staticShader->setMat4("lightSpaceMatrices[" + std::to_string(index) +"]", lightMatrix);
        m_shadowSystem.bindShadowPass(light, textureSlot);
    }

    staticShader->unbind();

    skeletonShader->bind();
    skeletonShader->setMat4("view", m_frameData.viewMatrix);
    skeletonShader->setMat4("projection", m_frameData.projectionMatrix);
    skeletonShader->setVec3("viewPos", m_frameData.cameraPosition);
    // LightManager::instance().sendLightsIntoShader(*skeletonShader);
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
        
        if (justShader == skeletonStencilShader)
        {
            const std::vector<glm::mat4>& boneMatrices = m_selectedGameObject->getComponent<MeshComponent>()->getModel()->getSkeleton()->getFinalMatrices();
            justShader->setMat4Array("finalBonesMatrices", boneMatrices);
        }

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

}

void Renderer::setSelectedGameObject(GameObject *gameObject)
{
    m_selectedGameObject = gameObject;
}

void Renderer::renderShadowPass(const std::vector<std::shared_ptr<GameObject>> &gameObjects)
{
    if(LightManager::instance().getLights().empty())
        return;

    const auto* staticShadowShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::STATIC_SHADOW);
    const auto* skeletonShadowShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::SKELETON_SHADOW);

    for(const auto& light : LightManager::instance().getLights())
    {
        m_shadowSystem.updateLightMatrix(light);

        m_shadowSystem.beginShadowPass(light);

        glm::mat4 lightMatrix = m_shadowSystem.getLightMatrix(light);

        for (const auto& gameObject : gameObjects)
        {
            const auto justShader = gameObject->getComponent<MeshComponent>()->getModel()->hasSkeleton() ? skeletonShadowShader : staticShadowShader;
            
            justShader->bind();
            justShader->setMat4("model", gameObject->getTransformMatrix());
            justShader->setMat4("lightSpaceMatrix", lightMatrix);

            if (justShader == skeletonShadowShader)
            {
                const std::vector<glm::mat4>& boneMatrices = gameObject->getComponent<MeshComponent>()->getModel()->getSkeleton()->getFinalMatrices();
                justShader->setMat4Array("finalBonesMatrices", boneMatrices);
            }

            gameObject->getComponent<MeshComponent>()->render();
            justShader->unbind();
       }

        m_shadowSystem.endShadowPass();
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
    m_shadowSystem.init(LightManager::instance().getLights(), elix::ShadowSystem::ShadowQuality::HIGH);
}
