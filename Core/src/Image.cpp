#include "Core/Image.hpp"
#include "Core/VulkanContext.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)

Image::Image(uint32_t width, uint32_t height, VkImageUsageFlags usage, memory::MemoryUsage memFlags, VkFormat format,
             VkImageTiling tiling, uint32_t arrayLayers, VkImageCreateFlags flags) : m_device(VulkanContext::getContext()->getDevice())
{
    createVk(VkExtent2D{.width = width, .height = height}, usage, memFlags, format, tiling, arrayLayers, flags);
}

void Image::createVk(VkExtent2D extent, VkImageUsageFlags usage, memory::MemoryUsage memFlags, VkFormat format,
                     VkImageTiling tiling, uint32_t arrayLayers, VkImageCreateFlags flags)
{
    ELIX_VK_CREATE_GUARD()

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = extent.width;
    imageInfo.extent.height = extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = flags;

    m_allocatedImage = VulkanContext::getContext()->getDevice()->createImage(imageInfo, memFlags);
    m_handle = m_allocatedImage.image;

    ELIX_VK_CREATE_GUARD_DONE()
}

void Image::bind(VkDeviceSize memoryOffset)
{
    VulkanContext::getContext()->getDevice()->bindImageMemory(m_allocatedImage, memoryOffset);
}

Image::Image(VkImage image) : m_device(VulkanContext::getContext()->getDevice())
{
    m_handle = image;
    m_allocatedImage.image = image;
    m_isWrapped = true;
}

void Image::copyBufferToImage(Buffer::SharedPtr buffer, uint32_t width, uint32_t height, CommandPool::SharedPtr commandPool, VkQueue queue, uint32_t layerCount)
{
    auto cb = CommandBuffer::createShared(commandPool);
    cb->begin();

    VkDeviceSize offset = 0;
    std::vector<VkBufferImageCopy> regions;
    for (uint32_t layer = 0; layer < layerCount; ++layer)
    {
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = layer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        regions.push_back(region);
        offset += width * height * 4;
    }

    vkCmdCopyBufferToImage(cb, buffer->vk(), m_handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()), regions.data());

    cb->end();

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = 0;

    VkFence fence = VK_NULL_HANDLE;

    if (vkCreateFence(core::VulkanContext::getContext()->getDevice(), &fenceInfo, nullptr, &fence) != VK_SUCCESS)
        std::cerr << "Failed to create fence for image memory barrier. Falling back to vkQueueWaitIdle\n";

    cb->submit(queue, {}, {}, {}, fence);

    if (fence)
    {
        vkWaitForFences(core::VulkanContext::getContext()->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(core::VulkanContext::getContext()->getDevice(), fence, nullptr);
    }
    else
        vkQueueWaitIdle(queue);
}

void Image::insertImageMemoryBarrier(VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
                                     VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                                     VkImageSubresourceRange subresourceRange, CommandPool::SharedPtr commandPool, VkQueue queue)
{
    if (!commandPool)
        commandPool = core::VulkanContext::getContext()->getGraphicsCommandPool();

    if (!queue)
        queue = VulkanContext::getContext()->getGraphicsQueue();

    auto cb = CommandBuffer::createShared(commandPool);
    cb->begin();

    VkImageMemoryBarrier imageMemoryBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.srcAccessMask = srcAccessMask;
    imageMemoryBarrier.dstAccessMask = dstAccessMask;
    imageMemoryBarrier.oldLayout = oldImageLayout;
    imageMemoryBarrier.newLayout = newImageLayout;
    imageMemoryBarrier.image = m_handle;
    imageMemoryBarrier.subresourceRange = subresourceRange;

    vkCmdPipelineBarrier(*cb, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

    cb->end();

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = 0;

    VkFence fence = VK_NULL_HANDLE;

    if (vkCreateFence(core::VulkanContext::getContext()->getDevice(), &fenceInfo, nullptr, &fence) != VK_SUCCESS)
        std::cerr << "Failed to create fence for image memory barrier. Falling back to vkQueueWaitIdle\n";

    cb->submit(queue, {}, {}, {}, fence);

    if (fence)
    {
        vkWaitForFences(core::VulkanContext::getContext()->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(core::VulkanContext::getContext()->getDevice(), fence, nullptr);
    }
    else
        vkQueueWaitIdle(queue);
}

void Image::transitionImageLayout(VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, CommandPool::SharedPtr commandPool, VkQueue queue, uint32_t layerCount)
{
    if (!commandPool)
        commandPool = core::VulkanContext::getContext()->getGraphicsCommandPool();

    if (!queue)
        queue = VulkanContext::getContext()->getGraphicsQueue();

    auto cb = CommandBuffer::createShared(commandPool);
    cb->begin();

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_handle;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        if (helpers::hasStencilComponent(format))
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    else
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else
        throw std::runtime_error("Unsuported layout transition");

    vkCmdPipelineBarrier(cb->vk(), sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    cb->end();

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = 0;

    VkFence fence = VK_NULL_HANDLE;

    if (vkCreateFence(core::VulkanContext::getContext()->getDevice(), &fenceInfo, nullptr, &fence) != VK_SUCCESS)
        std::cerr << "Failed to create fence for image memory barrier. Falling back to vkQueueWaitIdle\n";

    cb->submit(queue, {}, {}, {}, fence);

    if (fence)
    {
        vkWaitForFences(core::VulkanContext::getContext()->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(core::VulkanContext::getContext()->getDevice(), fence, nullptr);
    }
    else
        vkQueueWaitIdle(queue);
}

void Image::destroyVkImpl()
{
    if (!m_isWrapped)
    {
        VulkanContext::getContext()->getDevice()->destroyImage(m_allocatedImage);
        m_handle = m_allocatedImage.image;
    }
}

Image::~Image()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END