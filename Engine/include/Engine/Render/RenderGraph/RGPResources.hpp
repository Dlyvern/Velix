#ifndef ELIX_RGP_RESOURCES_HPP
#define ELIX_RGP_RESOURCES_HPP

#include "Core/Macros.hpp"

#include <volk.h>

#include <string>
#include <cstdint>
#include <vector>
#include <functional>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

enum class RGPTextureUsage
{
    SAMPLED,
    COLOR_ATTACHMENT,
    DEPTH_STENCIL,

    // RAW VULKAN FOR NOW
    COLOR_ATTACHMENT_TRANSFER_SRC
};

struct RGPResourceHandler
{
    uint32_t id;

    operator uint32_t() const { return id; }

    bool operator==(const RGPResourceHandler &other) const noexcept
    {
        return id == other.id;
    }
};

struct RGPResourceAccess
{
    RGPResourceHandler resourceId;
    RGPTextureUsage usage;
};

struct RGPPassInfo
{
    std::vector<RGPResourceAccess> reads;
    std::vector<RGPResourceAccess> writes;
};

struct SharedRGPResourceHandler
{
    RGPResourceHandler handler;
};

class RGPTextureDescription
{
public:
    RGPTextureDescription() = default;

    RGPTextureDescription(VkFormat format, RGPTextureUsage usage, VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                          VkImageLayout finalLayout = VK_IMAGE_LAYOUT_UNDEFINED)
    {
        m_Format = format;
        m_Usage = usage;
        m_InitialLayout = initialLayout;
        m_FinalLayout = finalLayout;
    }

    PROPERTY_FULL(VkFormat, Format)
    PROPERTY_FULL(std::string, DebugName)
    PROPERTY_FULL(RGPTextureUsage, Usage)
    PROPERTY_FULL(VkExtent2D, Extent)
    PROPERTY_FULL_DEFAULT(bool, IsSwapChainTarget, false);
    PROPERTY_FULL_DEFAULT(std::function<VkExtent2D()>, CustomExtentFunction, nullptr); // If not nullptr, Extent field will be ignored
    PROPERTY_FULL_DEFAULT(bool, IsDepenedOnSwapChainSize, false);                      // On swap chain resize this resource will be re-created with swap chain extent
    PROPERTY_FULL_DEFAULT(VkImageLayout, InitialLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    PROPERTY_FULL_DEFAULT(VkImageLayout, FinalLayout, VK_IMAGE_LAYOUT_UNDEFINED);
};

ELIX_CUSTOM_NAMESPACE_END

ELIX_NESTED_NAMESPACE_END

namespace std
{
    template <>
    struct hash<elix::engine::renderGraph::RGPResourceHandler>
    {
        size_t operator()(elix::engine::renderGraph::RGPResourceHandler const &handler) const noexcept
        {
            return hash<uint32_t>()(handler.id);
        }
    };
}

#endif // ELIX_RGP_RESOURCES_HPP
