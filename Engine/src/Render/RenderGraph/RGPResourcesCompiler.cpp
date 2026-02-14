#include "Engine/Render/RenderGraph/RGPResourcesCompiler.hpp"

#include "Core/VulkanContext.hpp"

namespace
{
    static VkImageUsageFlags toVkUsage(elix::engine::renderGraph::RGPTextureUsage usage)
    {
        switch (usage)
        {
        case elix::engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC:
            return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        case elix::engine::renderGraph::RGPTextureUsage::SAMPLED:
            return VK_IMAGE_USAGE_SAMPLED_BIT;

        case elix::engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT:
            return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT;

        case elix::engine::renderGraph::RGPTextureUsage::DEPTH_STENCIL:
            return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        return 0;
    }

    static VkImageAspectFlags chooseAspect(VkFormat format)
    {
        if (format == VK_FORMAT_D32_SFLOAT ||
            format == VK_FORMAT_D24_UNORM_S8_UINT ||
            format == VK_FORMAT_D32_SFLOAT_S8_UINT)
        {
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        }

        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

void RGPResourcesCompiler::compile(RGPResourcesBuilder &builder, RGPResourcesStorage &storage)
{
    const auto &vulkanContext = core::VulkanContext::getContext();
    const auto &device = vulkanContext->getDevice();

    for (const auto &[id, textureDescription] : builder.getAllTextureDescriptions())
    {
        // TODO namespace above core::memory::MemoryUsage

        VkDeviceSize before = core::VulkanContext::getContext()->getDevice()->getTotalAllocatedVRAM();

        if (textureDescription.getIsSwapChainTarget())
        {
            const auto &swapChain = vulkanContext->getSwapchain();

            for (int imageIndex = 0; imageIndex < swapChain->getImages().size(); ++imageIndex)
            {
                const auto &image = swapChain->getImages().at(imageIndex);
                auto wrapImage = core::Image::createShared(image);

                // RenderTarget renderTarget(device, swapChain->getImageFormat(),
                //                           chooseAspect(textureDescription.getFormat()), wrapImage);

                auto renderTarget = std::make_shared<RenderTarget>(device, swapChain->getImageFormat(),
                                                                   chooseAspect(textureDescription.getFormat()), wrapImage);

                storage.addSwapChainTexture(id, std::move(renderTarget));
            }
        }
        else
        {
            // RenderTarget renderTarget(device, textureDescription.getExtent(), textureDescription.getFormat(), toVkUsage(textureDescription.getUsage()),
            //                           chooseAspect(textureDescription.getFormat()), core::memory::MemoryUsage::AUTO);
            // renderTarget.createVkImageView();

            auto renderTarget = std::make_shared<RenderTarget>(device, textureDescription.getExtent(), textureDescription.getFormat(), toVkUsage(textureDescription.getUsage()),
                                                               chooseAspect(textureDescription.getFormat()), core::memory::MemoryUsage::AUTO);
            renderTarget->createVkImageView();

            storage.addTexture(id, std::move(renderTarget));
        }

        VkDeviceSize after = core::VulkanContext::getContext()->getDevice()->getTotalAllocatedVRAM();
        VkDeviceSize delta = after - before;
        const std::string textureName = textureDescription.getIsSwapChainTarget() ? "swap chain" : "common";
        std::cout << "New " << textureName << " " << textureDescription.getDebugName() << " texture allocated " << delta << '\n';
    }
}

void RGPResourcesCompiler::onSwapChainResized(RGPResourcesBuilder &builder, RGPResourcesStorage &storage)
{
    const auto &vulkanContext = core::VulkanContext::getContext();
    const auto &device = vulkanContext->getDevice();

    for (const auto &[id, textureDescription] : builder.getAllTextureDescriptions())
    {
        if (!textureDescription.getIsSwapChainTarget())
            continue;

        const auto &swapChain = vulkanContext->getSwapchain();

        for (int imageIndex = 0; imageIndex < swapChain->getImages().size(); ++imageIndex)
        {
            const auto &image = swapChain->getImages().at(imageIndex);
            auto wrapImage = core::Image::createShared(image);

            // RenderTarget renderTarget(device, swapChain->getImageFormat(),
            //                           chooseAspect(textureDescription.getFormat()), wrapImage);

            auto renderTarget = std::make_shared<RenderTarget>(device, swapChain->getImageFormat(),
                                                               chooseAspect(textureDescription.getFormat()), wrapImage);

            storage.addSwapChainTexture(id, std::move(renderTarget), imageIndex);
        }
    }
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
