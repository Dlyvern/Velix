#include "Engine/Render/RenderGraphPassResourceHash.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void RenderGraphPassResourceHash::addTexture(std::size_t hash, core::Texture::SharedPtr texture)
{
    m_textures[hash] = texture;
}

void RenderGraphPassResourceHash::addGraphicsPipeline(std::size_t hash, core::GraphicsPipeline::SharedPtr graphicsPipeline)
{
    m_graphicsPipelines[hash] = graphicsPipeline;
}

void RenderGraphPassResourceHash::addFramebuffer(std::size_t hash, core::Framebuffer::SharedPtr framebuffer)
{
    m_framebuffers[hash] = framebuffer;
}

void RenderGraphPassResourceHash::addRenderPass(std::size_t hash, core::RenderPass::SharedPtr renderPass)
{
    m_renderPasses[hash] = renderPass;
}

core::Texture::SharedPtr RenderGraphPassResourceHash::getTexture(std::size_t hash) const
{
    auto it = m_textures.find(hash);

    return it != m_textures.end() ? it->second : nullptr;
}

core::GraphicsPipeline::SharedPtr RenderGraphPassResourceHash::getGraphicsPipeline(std::size_t hash) const
{
    auto it = m_graphicsPipelines.find(hash);

    return it != m_graphicsPipelines.end() ? it->second : nullptr;
}

core::RenderPass::SharedPtr RenderGraphPassResourceHash::getRenderPass(std::size_t hash) const
{
    auto it = m_renderPasses.find(hash);

    return it != m_renderPasses.end() ? it->second : nullptr;
}

core::Framebuffer::SharedPtr RenderGraphPassResourceHash::getFramebuffer(std::size_t hash) const
{
    auto it = m_framebuffers.find(hash);

    return it != m_framebuffers.end() ? it->second : nullptr;
}

void RenderGraphPassResourceHash::cleanup()
{
    m_framebuffers.clear();
    m_graphicsPipelines.clear();
    m_renderPasses.clear();
    m_textures.clear();
}

ELIX_NESTED_NAMESPACE_END