#ifndef ELIX_RGP_RESOURCES_STORAGE_HPP
#define ELIX_RGP_RESOURCES_STORAGE_HPP

#include "Engine/Render/RenderGraph/RGPResources.hpp"
#include "Engine/Render/RenderTarget.hpp"

#include <unordered_map>
#include <vector>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class RGPResourcesStorage
{
public:
    void addTexture(const RGPResourceHandler &handler, const std::shared_ptr<RenderTarget> &texture);

    void addSwapChainTexture(const RGPResourceHandler &handler, const std::shared_ptr<RenderTarget> &texture, int index = -1);

    const RenderTarget *getTexture(const RGPResourceHandler &handler) const;
    const RenderTarget *getSwapChainTexture(const RGPResourceHandler &handler, int imageIndex) const;

    RenderTarget *getSwapChainTexture(const RGPResourceHandler &handler, int imageIndex);
    RenderTarget *getTexture(const RGPResourceHandler &handler);

    void cleanup();

private:
    std::unordered_map<RGPResourceHandler, std::shared_ptr<RenderTarget>> m_textures;
    std::unordered_map<RGPResourceHandler, std::vector<std::shared_ptr<RenderTarget>>> m_swapChainTextures;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RGP_RESOURCES_STORAGE_HPP