#ifndef ELIX_SWAP_CHAIN_RENDER_GRAPH_PROXY_HPP
#define ELIX_SWAP_CHAIN_RENDER_GRAPH_PROXY_HPP

#include "Core/Macros.hpp"
#include "Core/RenderPass.hpp"

#include <volk.h>

#include "Engine/Render/Proxies/IRenderGraphProxy.hpp"
#include "Engine/Render/Proxies/ImageRenderGraphProxy.hpp"
#include "Engine/Render/Proxies/RenderPassRenderGraphProxy.hpp"

#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class SwapChainRenderGraphProxy : public IRenderGraphProxy<VkFramebuffer, RenderGraphProxyContainerData, VkFramebuffer>
{
public:
    using SharedPtr = std::shared_ptr<SwapChainRenderGraphProxy>;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;

    RenderPassRenderGraphProxy::SharedPtr renderPassProxy{nullptr};

    std::vector<ImageRenderGraphProxy::SharedPtr> additionalImages;

    uint32_t currentImageIndex{0};

    explicit SwapChainRenderGraphProxy(const std::string& name) : IRenderGraphProxy(name) {}
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_SWAP_CHAIN_RENDER_GRAPH_PROXY_HPP