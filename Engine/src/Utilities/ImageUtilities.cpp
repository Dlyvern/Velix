#include "Engine/Utilities/ImageUtilities.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(utilities)

ImageUtilities::LayoutTransitionInfo ImageUtilities::getSrcLayoutInfo(VkImageLayout layout)
{
    switch (layout)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        // Nothing to wait for - transition happens at the beginning
        return {0, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};

    case VK_IMAGE_LAYOUT_GENERAL:
        // Could be used by any stage
        return {VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        // Was used as color attachment
        return {VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};

    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return {VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT};

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        // Was read by shaders
        return {VK_ACCESS_2_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        // Was used as transfer source
        return {VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT};

    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        // Was used as transfer destination
        return {VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT};

    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        return {0, VK_PIPELINE_STAGE_2_NONE};

    default:
        return {VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    }
}

ImageUtilities::LayoutTransitionInfo ImageUtilities::getDstLayoutInfo(VkImageLayout layout)
{
    switch (layout)
    {
        // Not a valid destination layout (except for discard)
    case VK_IMAGE_LAYOUT_UNDEFINED:
        return {0, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};

    case VK_IMAGE_LAYOUT_GENERAL:
        // Will be used by any stage
        return {VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        // Will be used as color attachment
        return {VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};

    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return {VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT};

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        // Will be read by shaders
        return {VK_ACCESS_2_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        // Will be used as transfer source
        return {VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT};

    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        // Will be used as transfer destination
        return {VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT};

    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        return {0, VK_PIPELINE_STAGE_2_NONE};

    default:
        return {VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    }
}

VkImageMemoryBarrier2 ImageUtilities::insertImageMemoryBarrier(core::Image &image, core::CommandBuffer &commandBuffer, VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask,
                                                               VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkPipelineStageFlags2 srcStageMask, VkPipelineStageFlags2 dstStageMask,
                                                               VkImageSubresourceRange subresourceRange)
{
    VkImageMemoryBarrier2 imgBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    imgBarrier.srcStageMask = srcStageMask;
    imgBarrier.srcAccessMask = srcAccessMask;
    imgBarrier.dstStageMask = dstStageMask;
    imgBarrier.dstAccessMask = dstAccessMask;
    imgBarrier.oldLayout = oldImageLayout;
    imgBarrier.newLayout = newImageLayout;
    imgBarrier.image = image;
    imgBarrier.subresourceRange = subresourceRange;

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &imgBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &dep);

    return imgBarrier;
}

VkImageMemoryBarrier2 ImageUtilities::insertImageMemoryBarrier(core::Image &image, VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask,
                                                               VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkPipelineStageFlags2 srcStageMask, VkPipelineStageFlags2 dstStageMask,
                                                               VkImageSubresourceRange subresourceRange)
{
    VkImageMemoryBarrier2 imgBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    imgBarrier.srcStageMask = srcStageMask;
    imgBarrier.srcAccessMask = srcAccessMask;
    imgBarrier.dstStageMask = dstStageMask;
    imgBarrier.dstAccessMask = dstAccessMask;
    imgBarrier.oldLayout = oldImageLayout;
    imgBarrier.newLayout = newImageLayout;
    imgBarrier.image = image;
    imgBarrier.subresourceRange = subresourceRange;

    return imgBarrier;
}

void ImageUtilities::copyImageToBuffer(core::Image &image, core::Buffer &buffer, core::CommandBuffer &commandBuffer, VkOffset3D imageOffset)
{
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // TODO change it to depth aspect(pass core::Image instead of raw vulkan)
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = imageOffset;
    region.imageExtent = {1, 1, 1};

    vkCmdCopyImageToBuffer(
        commandBuffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        buffer,
        1,
        &region);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
