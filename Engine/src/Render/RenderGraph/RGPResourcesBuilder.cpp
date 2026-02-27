#include "Engine/Render/RenderGraph/RGPResourcesBuilder.hpp"
#include "Core/Logger.hpp"

#include <iostream>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

RGPResourceHandler RGPResourcesBuilder::createTexture(const RGPTextureDescription &description)
{
    ++m_nextResourceId;
    m_textureDescriptions[RGPResourceHandler{.id = m_nextResourceId}] = description;
    return RGPResourceHandler{.id = m_nextResourceId};
}

RGPResourceHandler RGPResourcesBuilder::createTexture(const RGPTextureDescription &description, RGPResourceHandler &handler)
{
    ++m_nextResourceId;
    handler.id = m_nextResourceId;
    m_textureDescriptions[handler] = description;
    return handler;
}

const RGPTextureDescription *RGPResourcesBuilder::getTextureDescription(const RGPResourceHandler &handler) const
{
    auto it = m_textureDescriptions.find(handler);

    return it == m_textureDescriptions.end() ? nullptr : &it->second;
}

const std::unordered_map<RGPResourceHandler, RGPTextureDescription> &RGPResourcesBuilder::getAllTextureDescriptions() const
{
    return m_textureDescriptions;
}

void RGPResourcesBuilder::setCurrentPass(RGPPassInfo *pass)
{
    m_currentPass = pass;
}

void RGPResourcesBuilder::write(const RGPResourceHandler &handler, RGPTextureUsage usage)
{
    if (!m_currentPass)
    {
        VX_ENGINE_ERROR_STREAM("No current pass(Write abort)\n");
        return;
    }

    m_currentPass->writes.emplace_back(RGPResourceAccess{.resourceId = handler.id, .usage = usage});
}

void RGPResourcesBuilder::read(const RGPResourceHandler &handler, RGPTextureUsage usage)
{
    if (!m_currentPass)
    {
        VX_ENGINE_ERROR_STREAM("No current pass(Read abort)\n");
        return;
    }

    m_currentPass->reads.emplace_back(RGPResourceAccess{.resourceId = handler.id, .usage = usage});
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
