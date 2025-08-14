// #include <glad/glad.h>
#include "StencilRender.hpp"
#include <VelixFlow/Components/MeshComponent.hpp>
// #include <VelixGL/ShaderManager.hpp>
// #include <VelixGL/DrawCall.hpp>
#include <VelixFlow/Scene.hpp>

// #include <VelixGL/GLMesh.hpp>

#include <VelixFlow/Components/TransformComponent.hpp>

bool StencilRender::shouldExecute() const
{
    return true;
}

std::string StencilRender::getName() const
{
    return "StencilRender";
}

void StencilRender::render(const elix::render::FrameData& frameData, elix::Scene* scene)
{
    if(!m_selectedGameObject)
        return;

    // if(renderTarget_)
    //     renderTarget_->bind();    

    // glEnable(GL_STENCIL_TEST);
    // glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    // auto* staticStencilShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::STATIC_STENCIL);
    // auto* skeletonStencilShader = ShaderManager::instance().getShader(ShaderManager::ShaderType::SKELETON_STENCIL);

    // staticStencilShader->bind();
    // staticStencilShader->setMat4("view", frameData.viewMatrix);
    // staticStencilShader->setMat4("projection", frameData.projectionMatrix);
    // staticStencilShader->setVec3("viewPos", frameData.cameraPosition);
    // staticStencilShader->unbind();

    // skeletonStencilShader->bind();
    // skeletonStencilShader->setMat4("view", frameData.viewMatrix);
    // skeletonStencilShader->setMat4("projection", frameData.projectionMatrix);
    // skeletonStencilShader->setVec3("viewPos", frameData.cameraPosition);
    // skeletonStencilShader->unbind();

    if (auto meshComponent = m_selectedGameObject->getComponent<elix::components::MeshComponent>())
    {
        // glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        // glStencilMask(0x00);
        // glDisable(GL_DEPTH_TEST);

        // const glm::mat4& model = glm::scale(m_selectedGameObject->getComponent<elix::components::TransformComponent>()->getTransformMatrix(), glm::vec3(1.05f));

        // const auto objectModel = meshComponent->getModel();
        // const bool isSkeleton = objectModel->hasSkeleton();
        // const auto justShader = isSkeleton ? skeletonStencilShader : staticStencilShader;

        // justShader->bind();

        // if (isSkeleton)
        // {
        //     const std::vector<glm::mat4> &boneMatrices = objectModel->getSkeleton()->getFinalMatrices();
        //     justShader->setMat4Array("finalBonesMatrices", boneMatrices);
        // }

        // justShader->setMat4("model", model);

        // for (int meshIndex = 0; meshIndex < objectModel->getNumMeshes(); meshIndex++)
        // {
        //     auto imesh = objectModel->getMesh(meshIndex);

        //     auto mesh = dynamic_cast<elix::GLMesh*>(imesh.get());


        //     const auto& vertexArray = mesh->getVertexArray();
        //     vertexArray.bind();
        //     elix::DrawCall::draw(elix::DrawCall::DrawMode::TRIANGLES, mesh->getIndices().size(), elix::DrawCall::DrawType::UNSIGNED_INT, nullptr);
        //     vertexArray.unbind();
        // }

        // justShader->unbind();

        // glEnable(GL_DEPTH_TEST);
        // glStencilMask(0xFF);
        // glStencilFunc(GL_ALWAYS, 1, 0xFF);
    }

    // if(renderTarget_)
    //     renderTarget_->unbind();
}

void StencilRender::setSelectedGameObject(elix::GameObject* gameObject)
{
    m_selectedGameObject = gameObject;
}


int StencilRender::getPriority() const
{
    return 100;
}
