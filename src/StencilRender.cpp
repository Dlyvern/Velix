#include <glad/glad.h>
#include "StencilRender.hpp"
#include <VelixFlow/MeshComponent.hpp>

bool StencilRender::shouldExecute() const
{
    return true;
}

std::string StencilRender::getName() const
{
    return "StencilRender";
}

void StencilRender::render(const elix::FrameData& frameData, Scene* scene)
{
    if(!m_selectedGameObject)
        return;

    if(renderTarget_)
        renderTarget_->bind();


    

    glEnable(GL_STENCIL_TEST);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    elix::Shader *staticStencilShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::STATIC_STENCIL);
    elix::Shader *skeletonStencilShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::SKELETON_STENCIL);

    staticStencilShader->bind();
    staticStencilShader->setMat4("view", frameData.viewMatrix);
    staticStencilShader->setMat4("projection", frameData.projectionMatrix);
    staticStencilShader->setVec3("viewPos", frameData.cameraPosition);
    staticStencilShader->unbind();

    skeletonStencilShader->bind();
    skeletonStencilShader->setMat4("view", frameData.viewMatrix);
    skeletonStencilShader->setMat4("projection", frameData.projectionMatrix);
    skeletonStencilShader->setVec3("viewPos", frameData.cameraPosition);
    skeletonStencilShader->unbind();

    if (m_selectedGameObject->hasComponent<MeshComponent>())
    {
        glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        glStencilMask(0x00);
        glDisable(GL_DEPTH_TEST);

        glm::mat4 model = glm::scale(m_selectedGameObject->getTransformMatrix(), glm::vec3(1.05f));

        const auto *justShader = m_selectedGameObject->getComponent<MeshComponent>()->getModel()->hasSkeleton() ? skeletonStencilShader : staticStencilShader;

        justShader->bind();

        if (justShader == skeletonStencilShader)
        {
            const std::vector<glm::mat4> &boneMatrices = m_selectedGameObject->getComponent<MeshComponent>()->getModel()->getSkeleton()->getFinalMatrices();
            justShader->setMat4Array("finalBonesMatrices", boneMatrices);
        }

        justShader->setMat4("model", model);
        m_selectedGameObject->getComponent<MeshComponent>()->render();
        justShader->unbind();

        glEnable(GL_DEPTH_TEST);
        glStencilMask(0xFF);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
    }

    if(renderTarget_)
        renderTarget_->unbind();
}

void StencilRender::setSelectedGameObject(GameObject* gameObject)
{
    m_selectedGameObject = gameObject;
}


int StencilRender::getPriority() const
{
    return 100;
}
