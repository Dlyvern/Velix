#ifndef ELIX_BUFFER_UTILITIES_HPP
#define ELIX_BUFFER_UTILITIES_HPP

#include "Core/Buffer.hpp"
#include "Core/Image.hpp"
#include "Core/CommandBuffer.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(utilities)

class BufferUtilities
{
public:
    static void copyBufferToImage(core::Buffer &buffer, core::Image &image, core::CommandBuffer &commandBuffer, VkExtent2D extent,
                                  VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT,
                                  VkImageLayout dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, uint32_t layerCount = 1, uint32_t baseLayer = 0);
    static void copyBuffer(core::Buffer &srcBuffer, core::Buffer &dstBuffer, core::CommandBuffer &commandBuffer, VkDeviceSize size);
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_BUFFER_UTILITIES_HPP