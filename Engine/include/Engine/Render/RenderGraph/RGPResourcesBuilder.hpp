#ifndef ELIX_RGP_RESOURCES_BUILDER_HPP
#define ELIX_RGP_RESOURCES_BUILDER_HPP

#include "Engine/Render/RenderGraph/RGPResources.hpp"

#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class RGPResourcesBuilder
{
public:
    RGPResourceHandler createTexture(const RGPTextureDescription &description);
    RGPResourceHandler createTexture(const RGPTextureDescription &description, RGPResourceHandler &handler);

    void setCurrentPass(RGPPassInfo *pass);

    void write(const RGPResourceHandler &handler, RGPTextureUsage usage);
    void read(const RGPResourceHandler &handler, RGPTextureUsage usage);

    const std::unordered_map<RGPResourceHandler, RGPTextureDescription> &getAllTextureDescriptions() const;

private:
    uint32_t m_nextResourceId{0};
    RGPPassInfo *m_currentPass{nullptr};
    std::unordered_map<RGPResourceHandler, RGPTextureDescription> m_textureDescriptions;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RGP_RESOURCES_BUILDER_HPP