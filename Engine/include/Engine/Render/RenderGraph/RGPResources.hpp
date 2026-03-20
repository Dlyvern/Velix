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
    COLOR_ATTACHMENT_STORAGE,
    DEPTH_STENCIL,
    COLOR_ATTACHMENT_TRANSFER_SRC
};

struct RGPResourceHandler
{
    uint32_t id{UINT32_MAX};

    operator uint32_t() const { return id; }

    bool operator==(const RGPResourceHandler &other) const noexcept
    {
        return id == other.id;
    }

    bool isValid() const { return id != UINT32_MAX; }
};

using SingleHandle = RGPResourceHandler;
using MultiHandle = std::vector<RGPResourceHandler>;

class IRenderGraphPass;

template <typename T>
class RGPOutputSlot
{
public:
    void set(T handle)
    {
        m_handles = std::move(handle);
    }

    const T &get() const
    {
        return m_handles;
    }

    IRenderGraphPass *const getOwner() const
    {
        return m_owner;
    }

    void setOwner(IRenderGraphPass *owner)
    {
        m_owner = owner;
    }

private:
    T m_handles;
    IRenderGraphPass *m_owner{nullptr};
};

template <typename T>
class RGPInputSlot
{
public:
    void connectFrom(const RGPOutputSlot<T> &slot)
    {
        m_source = &slot;
    }

    const T &get() const
    {
        return m_source->get();
    }

    bool isConnected() const
    {
        return m_source != nullptr;
    }

private:
    const RGPOutputSlot<T> *m_source{nullptr};
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
    PROPERTY_FULL_DEFAULT(VkImageLayout, InitialLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    PROPERTY_FULL_DEFAULT(VkImageLayout, FinalLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    PROPERTY_FULL_DEFAULT(VkSampleCountFlagBits, SampleCount, VK_SAMPLE_COUNT_1_BIT);
    PROPERTY_FULL_DEFAULT(uint32_t, ArrayLayers, 1);
    PROPERTY_FULL_DEFAULT(VkImageCreateFlags, Flags, 0);
    PROPERTY_FULL_DEFAULT(VkImageViewType, ImageViewtype, VK_IMAGE_VIEW_TYPE_2D);
    PROPERTY_FULL_DEFAULT(bool, Aliasable, true);
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
