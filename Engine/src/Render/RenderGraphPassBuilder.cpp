#include "Engine/Render/RenderGraphPassBuilder.hpp"
#include <iostream>
#include <stdexcept>
ELIX_NESTED_NAMESPACE_BEGIN(engine)

void RenderGraphPassBuilder::createImageProxy(VkDevice device, ImageRenderGraphProxy* imageProxy)
{
    imageProxy->storage.data = std::make_shared<core::Image>(device, imageProxy->width, imageProxy->height, imageProxy->usage, imageProxy->properties,
    imageProxy->format, imageProxy->tiling);
    
    VkImageViewCreateInfo imageViewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

    imageViewCI.image = imageProxy->storage.data->vk();
    imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCI.format = imageProxy->format;
    imageViewCI.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.subresourceRange.aspectMask = imageProxy->aspect;
    imageViewCI.subresourceRange.baseMipLevel = 0;
    imageViewCI.subresourceRange.levelCount = 1;
    imageViewCI.subresourceRange.baseArrayLayer = 0;
    imageViewCI.subresourceRange.layerCount = 1;

    if(vkCreateImageView(device, &imageViewCI, nullptr, &imageProxy->imageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create image views");

    std::cout << "Created: " << imageProxy->getName() << std::endl;
}

void RenderGraphPassBuilder::createRenderPassProxy(VkDevice device, RenderPassRenderGraphProxy* renderPassProxy)
{
    renderPassProxy->storage.data = core::RenderPass::create(device, renderPassProxy->attachments, renderPassProxy->subpasses, renderPassProxy->dependencies);
}

ELIX_NESTED_NAMESPACE_END