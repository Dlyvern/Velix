#ifndef ELIX_IMAGE_UTILITIES_HPP
#define ELIX_IMAGE_UTILITIES_HPP

#include "Core/Image.hpp"
#include "Core/CommandBuffer.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(utilities)

class ImageUtilities
{
public:
    struct LayoutTransitionInfo
    {
        VkAccessFlags2 accessMask;
        VkPipelineStageFlags2 stageMask;
    };

    static LayoutTransitionInfo getSrcLayoutInfo(VkImageLayout layout);
    static LayoutTransitionInfo getDstLayoutInfo(VkImageLayout layout);

    static VkImageAspectFlags getAspectBasedOnFormat(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return VK_IMAGE_ASPECT_DEPTH_BIT;

        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }

    static void copyImageToBuffer(core::Image &image, core::Buffer &buffer, core::CommandBuffer &commandBuffer, VkOffset3D imageOffset);

    static VkImageMemoryBarrier2 insertImageMemoryBarrier(core::Image &image, core::CommandBuffer &commandBuffer, VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask,
                                                          VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkPipelineStageFlags2 srcStageMask, VkPipelineStageFlags2 dstStageMask,
                                                          VkImageSubresourceRange subresourceRange);

    static VkImageMemoryBarrier2 insertImageMemoryBarrier(core::Image &image, VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask,
                                                          VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkPipelineStageFlags2 srcStageMask, VkPipelineStageFlags2 dstStageMask,
                                                          VkImageSubresourceRange subresourceRange);
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_IMAGE_UTILITIES_HPP