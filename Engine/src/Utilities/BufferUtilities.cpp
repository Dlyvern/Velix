#include "Engine/Utilities/BufferUtilities.hpp"
#include "Core/VulkanContext.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(utilities)

void BufferUtilities::copyBufferToImage(core::Buffer &buffer, core::Image &image, core::CommandBuffer &commandBuffer, VkExtent2D extent, VkImageAspectFlags aspectFlags,
                                        VkImageLayout dstImageLayout, uint32_t layerCount, uint32_t baseLayer, uint32_t mipLevel)
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
        region.imageSubresource.mipLevel = mipLevel;
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

void BufferUtilities::copyBufferRegion(core::Buffer &srcBuffer, core::Buffer &dstBuffer, core::CommandBuffer &commandBuffer,
                                       VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset)
{
    VkBufferCopy copyRegion{
        .srcOffset = srcOffset,
        .dstOffset = dstOffset,
        .size = size};

    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
}

VkDeviceAddress BufferUtilities::getBufferDeviceAddress(const core::Buffer &buffer)
{
    const auto context = core::VulkanContext::getContext();
    if (!context || !context->hasBufferDeviceAddressSupport())
        return 0u;

    VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    addressInfo.buffer = buffer.vk();
    return vkGetBufferDeviceAddress(context->getDevice(), &addressInfo);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
