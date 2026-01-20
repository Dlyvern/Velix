#ifndef ELIX_RENDER_GRAPH_PASS_RESOURCE_HASH_HPP
#define ELIX_RENDER_GRAPH_PASS_RESOURCE_HASH_HPP

#include "Core/Macros.hpp"
#include "Engine/Render/RenderTarget.hpp"
#include "Core/Framebuffer.hpp"
#include "Core/RenderPass.hpp"
#include "Core/GraphicsPipeline.hpp"

#include <cstddef>

#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class RenderGraphPassResourceHash
{
public:
    void addTexture(std::size_t hash, RenderTarget::SharedPtr texture);
    void addFramebuffer(std::size_t hash, core::Framebuffer::SharedPtr framebuffer);
    void addRenderPass(std::size_t hash, core::RenderPass::SharedPtr renderPass);
    void addGraphicsPipeline(std::size_t hash, core::GraphicsPipeline::SharedPtr graphicsPipeline);

    core::Framebuffer::SharedPtr getFramebuffer(std::size_t hash) const;
    RenderTarget::SharedPtr getTexture(std::size_t hash) const;
    core::RenderPass::SharedPtr getRenderPass(std::size_t hash) const;
    core::GraphicsPipeline::SharedPtr getGraphicsPipeline(std::size_t hash) const;

    void cleanup();

private:
    std::unordered_map<std::size_t, RenderTarget::SharedPtr> m_textures;
    std::unordered_map<std::size_t, core::Framebuffer::SharedPtr> m_framebuffers;
    std::unordered_map<std::size_t, core::RenderPass::SharedPtr> m_renderPasses;
    std::unordered_map<std::size_t, core::GraphicsPipeline::SharedPtr> m_graphicsPipelines;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_GRAPH_PASS_RESOURCE_HASH_HPP