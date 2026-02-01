#ifndef ELIX_RGP_RESOURCES_HPP
#define ELIX_RGP_RESOURCES_HPP

#include "Core/Macros.hpp"

#include <volk.h>

#include <string>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

enum class RGPTextureUsage
{
    SAMPLED,
    COLOR_ATTACHMENT,
    DEPTH_STENCIL,
};

struct RGPResourceAccess
{
    uint32_t resourceId;
    RGPTextureUsage usage;
};

struct RGPPassInfo
{
    std::vector<RGPResourceAccess> reads;
    std::vector<RGPResourceAccess> writes;
};

struct RGPResourceHandler
{
    uint32_t id;

    bool operator==(const RGPResourceHandler &other) const noexcept
    {
        return id == other.id;
    }
};

struct SharedRGPResourceHandler
{
    RGPResourceHandler handler;
};

class RGPTextureDescription
{
    PROPERTY_FULL(VkFormat, Format)
    PROPERTY_FULL(std::string, DebugName)
    PROPERTY_FULL(RGPTextureUsage, Usage)
    PROPERTY_FULL(VkExtent2D, Extent)
    PROPERTY_FULL(bool, IsSwapChainTarget);
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
