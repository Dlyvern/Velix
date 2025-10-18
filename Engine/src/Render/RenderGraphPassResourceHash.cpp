#include "Engine/Render/RenderGraphPassResourceHash.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void RenderGraphPassResourceHash::addTexture(std::size_t hash, core::Texture<core::ImageNoDelete>::SharedPtr texture)
{
    m_textures[hash] = texture;
}

core::Texture<core::ImageNoDelete>::SharedPtr RenderGraphPassResourceHash::getTexture(std::size_t hash) const
{
    auto it = m_textures.find(hash);

    return it != m_textures.end() ? it->second : nullptr;
}

void RenderGraphPassResourceHash::addFramebuffer(std::size_t hash, core::Framebuffer::SharedPtr framebuffer)
{
    m_framebuffers[hash] = framebuffer;
}

void RenderGraphPassResourceHash::addRenderPass(std::size_t hash, core::RenderPass::SharedPtr renderPass)
{
    m_renderPasses[hash] = renderPass;
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


ELIX_NESTED_NAMESPACE_END