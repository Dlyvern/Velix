#ifndef ELIX_IMAGE_RENDER_GRAPH_PROXY_HPP
#define ELIX_IMAGE_RENDER_GRAPH_PROXY_HPP

#include "Core/Macros.hpp"
#include "Core/Image.hpp"

#include <volk.h>
#include "Engine/Render/Proxies/IRenderGraphProxy.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ImageRenderGraphProxy : public IRenderGraphProxy<core::Image, RenderGraphProxySinglePtrData, core::Image>
{
public:
    using SharedPtr = std::shared_ptr<ImageRenderGraphProxy>;
    using UniquePtr = std::unique_ptr<ImageRenderGraphProxy>;

    uint32_t width{0};
    uint32_t height{0};
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageView imageView{VK_NULL_HANDLE};

    bool isSwapChainImage{false};

    ImageRenderGraphProxy(const std::string& name) : IRenderGraphProxy(name) {}
};

class ImagesRenderGraphProxy : public IRenderGraphProxy<core::Image, RenderGraphProxyContainerPtrData, core::Image>
{
public:
    using SharedPtr = std::shared_ptr<ImageRenderGraphProxy>;
    uint32_t width{0};
    uint32_t height{0};
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    std::vector<VkImageView> imageViews;

    ImagesRenderGraphProxy(const std::string& name) : IRenderGraphProxy(name) {}
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_IMAGE_RENDER_GRAPH_PROXY_HPP