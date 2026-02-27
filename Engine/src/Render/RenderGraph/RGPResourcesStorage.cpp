#include "Engine/Render/RenderGraph/RGPResourcesStorage.hpp"
#include <iostream>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

void RGPResourcesStorage::addTexture(const RGPResourceHandler &handler, const std::shared_ptr<RenderTarget> &texture)
{
    // m_textures.emplace(handler, std::move(texture));
    m_textures[handler] = std::move(texture);
}

void RGPResourcesStorage::addSwapChainTexture(const RGPResourceHandler &handler, const std::shared_ptr<RenderTarget> &texture, int index)
{
    if (index == -1)
    {
        // m_swapChainTextures.emplace(handler, std::move(texture));
        m_swapChainTextures[handler].push_back(std::move(texture));
        return;
    }

    auto it = m_swapChainTextures.find(handler);

    if (it == m_swapChainTextures.end())
    {
        VX_ENGINE_ERROR_STREAM("Failed to find swap chain texture\n");
        return;
    }

    auto &vec = it->second;

    // TODO if image index is kinda good just push to the vector
    if (index > vec.size())
    {
        VX_ENGINE_ERROR_STREAM("Index is too high\n");
        return;
    }

    vec[index] = std::move(texture);
}

RenderTarget *RGPResourcesStorage::getSwapChainTexture(const RGPResourceHandler &handler, int imageIndex)
{
    auto it = m_swapChainTextures.find(handler);

    if (it == m_swapChainTextures.end())
        return nullptr;

    const auto &vec = it->second;

    if (imageIndex < 0 || static_cast<size_t>(imageIndex) >= vec.size())
        return nullptr;

    // return &vec[imageIndex];
    return vec[imageIndex].get();
}

RenderTarget *RGPResourcesStorage::getTexture(const RGPResourceHandler &handler)
{
    auto it = m_textures.find(handler);

    // return it == m_textures.end() ? nullptr : &(it->second);
    return it == m_textures.end() ? nullptr : it->second.get();
}

const RenderTarget *RGPResourcesStorage::getSwapChainTexture(const RGPResourceHandler &handler, int imageIndex) const
{
    auto it = m_swapChainTextures.find(handler);

    if (it == m_swapChainTextures.end())
        return nullptr;

    const auto &vec = it->second;

    if (imageIndex < 0 || static_cast<size_t>(imageIndex) >= vec.size())
        return nullptr;

    // return &vec[imageIndex];
    return vec[imageIndex].get();
}

const RenderTarget *RGPResourcesStorage::getTexture(const RGPResourceHandler &handler) const
{
    auto it = m_textures.find(handler);

    // return it == m_textures.end() ? nullptr : &(it->second);
    return it == m_textures.end() ? nullptr : it->second.get();
}

void RGPResourcesStorage::cleanup()
{
    m_textures.clear();
    m_swapChainTextures.clear();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
