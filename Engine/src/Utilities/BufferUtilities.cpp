#include "Engine/Utilities/BufferUtilities.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(utilities)

void BufferUtilities::copyBufferToImage(core::Buffer &buffer, core::Image &image, core::CommandBuffer &commandBuffer, VkExtent2D extent, VkImageAspectFlags aspectFlags,
                                        VkImageLayout dstImageLayout, uint32_t layerCount, uint32_t baseLayer)
{
    VkDeviceSize offset = 0;
    std::vector<VkBufferImageCopy> regions;
    regions.reserve(layerCount);

    for (uint32_t layer = 0; layer < layerCount; ++layer)
    {
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = aspectFlags;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = baseLayer + layer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {extent.width, extent.height, 1};

        regions.push_back(region);
        offset += extent.width * extent.height * 4 * sizeof(float);
    }

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, dstImageLayout, static_cast<uint32_t>(regions.size()), regions.data());
}

void BufferUtilities::copyBuffer(core::Buffer &srcBuffer, core::Buffer &dstBuffer, core::CommandBuffer &commandBuffer, VkDeviceSize size)
{
    VkBufferCopy copyRegion{
        .srcOffset = 0,
        .dstOffset = 0,
        .size = size};

    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
