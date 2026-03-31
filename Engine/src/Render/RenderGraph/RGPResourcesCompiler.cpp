#include "Engine/Render/RenderGraph/RGPResourcesCompiler.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/Logger.hpp"

#include "Engine/Utilities/ImageUtilities.hpp"

#include <unordered_set>

namespace
{
    static VkImageLayout chooseBootstrapLayout(VkImageLayout initialLayout, VkImageLayout finalLayout)
    {
        return finalLayout != VK_IMAGE_LAYOUT_UNDEFINED ? finalLayout : initialLayout;
    }

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

        case elix::engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT_STORAGE:
            return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                   VK_IMAGE_USAGE_STORAGE_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT;

        case elix::engine::renderGraph::RGPTextureUsage::DEPTH_STENCIL:
            return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        return 0;
    }

    static elix::engine::renderGraph::RGPResourceHandler resolveAliasRoot(
        const elix::engine::renderGraph::RGPResourceHandler &handler,
        const std::unordered_map<elix::engine::renderGraph::RGPResourceHandler,
                                 elix::engine::renderGraph::RGPResourceHandler> *aliasRoots)
    {
        if (!aliasRoots)
            return handler;

        const auto it = aliasRoots->find(handler);
        return it == aliasRoots->end() ? handler : it->second;
    }

    static void chooseDstSync(VkImageLayout layout,
                              VkPipelineStageFlags2 &dstStage,
                              VkAccessFlags2 &dstAccess)
    {
        switch (layout)
        {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            return;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            return;

        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            return;

        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            dstAccess = VK_ACCESS_2_TRANSFER_READ_BIT;
            return;

        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            dstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            return;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            return;

        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            dstStage = VK_PIPELINE_STAGE_2_NONE;
            dstAccess = 0;
            return;

        default:
            // Safe fallback: make it explicit if you forgot to handle something
            throw std::runtime_error("chooseDstSync: unsupported initial layout");
        }
    }
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

std::vector<VkImageMemoryBarrier2> RGPResourcesCompiler::compile(const std::vector<RGPResourceHandler> &resourcesIds,
                                                                 RGPResourcesBuilder &builder, RGPResourcesStorage &storage)
{
    const auto &vulkanContext = core::VulkanContext::getContext();
    const auto &device = vulkanContext->getDevice();

    std::vector<VkImageMemoryBarrier2> barriers;
    std::unordered_set<RenderTarget *> recreatedTargets;

    for (const auto &idHandler : resourcesIds)
    {
        auto textureDescription = builder.getTextureDescription(idHandler);

        if (!textureDescription)
            continue;

        if (textureDescription->getIsSwapChainTarget())
            continue;

        auto *renderTarget = storage.getTexture(idHandler);
        if (!renderTarget)
        {
            const auto extent = textureDescription->getCustomExtentFunction() ? textureDescription->getCustomExtentFunction()() : textureDescription->getExtent();
            auto newRenderTarget = std::make_shared<RenderTarget>(device, extent, textureDescription->getFormat(), toVkUsage(textureDescription->getUsage()),
                                                                  utilities::ImageUtilities::getAspectBasedOnFormat(textureDescription->getFormat()), core::memory::MemoryUsage::AUTO,
                                                                  VK_IMAGE_TILING_OPTIMAL, textureDescription->getArrayLayers(), textureDescription->getFlags(),
                                                                  textureDescription->getSampleCount());
            newRenderTarget->createVkImageView(textureDescription->getImageViewtype());
            storage.addTexture(idHandler, newRenderTarget);
            renderTarget = newRenderTarget.get();
        }
        else
        {
            if (!recreatedTargets.insert(renderTarget).second)
                continue;

            renderTarget->destroyVkImageView();
            renderTarget->destroyVkImage();

            const auto extent = textureDescription->getCustomExtentFunction() ? textureDescription->getCustomExtentFunction()() : textureDescription->getExtent();

            renderTarget->createVkImage(extent, toVkUsage(textureDescription->getUsage()), core::memory::MemoryUsage::AUTO, textureDescription->getFormat(),
                                        VK_IMAGE_TILING_OPTIMAL, textureDescription->getArrayLayers(), textureDescription->getFlags(),
                                        textureDescription->getSampleCount());
            renderTarget->createVkImageView(textureDescription->getImageViewtype());
        }

        if (textureDescription->getFinalLayout() == VK_IMAGE_LAYOUT_UNDEFINED && textureDescription->getInitialLayout() == VK_IMAGE_LAYOUT_UNDEFINED)
            continue;

        const auto &image = storage.getTexture(idHandler);

        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = utilities::ImageUtilities::getAspectBasedOnFormat(textureDescription->getFormat());
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = textureDescription->getArrayLayers();

        VkPipelineStageFlags2 srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        VkAccessFlags2 srcAccessMask = 0;
        VkPipelineStageFlags2 dstStageMask;
        VkAccessFlags2 dstAccessMask;

        const VkImageLayout bootstrapLayout = chooseBootstrapLayout(textureDescription->getInitialLayout(), textureDescription->getFinalLayout());

        chooseDstSync(bootstrapLayout, dstStageMask, dstAccessMask);

        auto barrier = utilities::ImageUtilities::insertImageMemoryBarrier(*image->getImage(), srcAccessMask, dstAccessMask, VK_IMAGE_LAYOUT_UNDEFINED,
                                                                           bootstrapLayout, srcStageMask,
                                                                           dstStageMask, subresourceRange);

        barriers.push_back(barrier);
    }

    return barriers;
}

std::vector<VkImageMemoryBarrier2> RGPResourcesCompiler::compile(RGPResourcesBuilder &builder, RGPResourcesStorage &storage,
                                                              const std::unordered_map<RGPResourceHandler, RGPResourceHandler> *aliasRoots)
{
    const auto &vulkanContext = core::VulkanContext::getContext();
    const auto &device = vulkanContext->getDevice();

    std::vector<VkImageMemoryBarrier2> barriers;
    std::unordered_map<RGPResourceHandler, std::shared_ptr<RenderTarget>> aliasedRenderTargets;
    std::unordered_set<RGPResourceHandler> bootstrappedRoots;

    for (const auto &[id, textureDescription] : builder.getAllTextureDescriptions())
    {
        if (textureDescription.getIsSwapChainTarget())
        {
            const auto &swapChain = vulkanContext->getSwapchain();

            for (int imageIndex = 0; imageIndex < swapChain->getImages().size(); ++imageIndex)
            {
                const auto &image = swapChain->getImages().at(imageIndex);
                auto wrapImage = core::Image::createShared(image);

                auto renderTarget = std::make_shared<RenderTarget>(device, swapChain->getExtent(), swapChain->getImageFormat(),
                                                                   utilities::ImageUtilities::getAspectBasedOnFormat(textureDescription.getFormat()), wrapImage);

                storage.addSwapChainTexture(id, std::move(renderTarget));

                if (textureDescription.getFinalLayout() == VK_IMAGE_LAYOUT_UNDEFINED && textureDescription.getInitialLayout() == VK_IMAGE_LAYOUT_UNDEFINED)
                    continue;

                VkImageSubresourceRange subresourceRange{};
                subresourceRange.aspectMask = utilities::ImageUtilities::getAspectBasedOnFormat(textureDescription.getFormat());
                subresourceRange.baseMipLevel = 0;
                subresourceRange.levelCount = 1;
                subresourceRange.baseArrayLayer = 0;
                subresourceRange.layerCount = 1;

                VkPipelineStageFlags2 srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                VkAccessFlags2 srcAccessMask = 0;
                VkPipelineStageFlags2 dstStageMask;
                VkAccessFlags2 dstAccessMask;

                const VkImageLayout bootstrapLayout = chooseBootstrapLayout(textureDescription.getInitialLayout(), textureDescription.getFinalLayout());

                chooseDstSync(bootstrapLayout, dstStageMask, dstAccessMask);

                auto barrier = utilities::ImageUtilities::insertImageMemoryBarrier(*wrapImage, srcAccessMask, dstAccessMask, VK_IMAGE_LAYOUT_UNDEFINED,
                                                                                   bootstrapLayout, srcStageMask,
                                                                                   dstStageMask, subresourceRange);

                barriers.push_back(barrier);
            }

            continue;
        }

        const RGPResourceHandler rootHandler = resolveAliasRoot(id, aliasRoots);
        auto aliasedTargetIt = aliasedRenderTargets.find(rootHandler);
        if (aliasedTargetIt == aliasedRenderTargets.end())
        {
            const VkExtent2D extent = textureDescription.getCustomExtentFunction() ? textureDescription.getCustomExtentFunction()() : textureDescription.getExtent();

            auto renderTarget = std::make_shared<RenderTarget>(device, extent, textureDescription.getFormat(), toVkUsage(textureDescription.getUsage()),
                                                               utilities::ImageUtilities::getAspectBasedOnFormat(textureDescription.getFormat()), core::memory::MemoryUsage::AUTO,
                                                               VK_IMAGE_TILING_OPTIMAL, textureDescription.getArrayLayers(), textureDescription.getFlags(),
                                                               textureDescription.getSampleCount());
            renderTarget->createVkImageView(textureDescription.getImageViewtype());

            aliasedTargetIt = aliasedRenderTargets.emplace(rootHandler, std::move(renderTarget)).first;
        }

        storage.addTexture(id, aliasedTargetIt->second);

        if (!bootstrappedRoots.insert(rootHandler).second)
            continue;

        if (textureDescription.getFinalLayout() == VK_IMAGE_LAYOUT_UNDEFINED && textureDescription.getInitialLayout() == VK_IMAGE_LAYOUT_UNDEFINED)
            continue;

        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = utilities::ImageUtilities::getAspectBasedOnFormat(textureDescription.getFormat());
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = textureDescription.getArrayLayers();

        VkPipelineStageFlags2 srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        VkAccessFlags2 srcAccessMask = 0;
        VkPipelineStageFlags2 dstStageMask;
        VkAccessFlags2 dstAccessMask;

        const VkImageLayout bootstrapLayout = chooseBootstrapLayout(textureDescription.getInitialLayout(), textureDescription.getFinalLayout());

        chooseDstSync(bootstrapLayout, dstStageMask, dstAccessMask);

        auto barrier = utilities::ImageUtilities::insertImageMemoryBarrier(*aliasedTargetIt->second->getImage(), srcAccessMask, dstAccessMask, VK_IMAGE_LAYOUT_UNDEFINED,
                                                                           bootstrapLayout, srcStageMask,
                                                                           dstStageMask, subresourceRange);

        barriers.push_back(barrier);
    }

    return barriers;
}

std::vector<VkImageMemoryBarrier2> RGPResourcesCompiler::onSwapChainResized(RGPResourcesBuilder &builder, RGPResourcesStorage &storage)
{
    const auto &vulkanContext = core::VulkanContext::getContext();
    const auto &device = vulkanContext->getDevice();
    std::vector<VkImageMemoryBarrier2> barriers;

    for (const auto &[id, textureDescription] : builder.getAllTextureDescriptions())
    {
        if (!textureDescription.getIsSwapChainTarget())
            continue;

        const auto &swapChain = vulkanContext->getSwapchain();

        for (int imageIndex = 0; imageIndex < swapChain->getImages().size(); ++imageIndex)
        {
            const auto &image = swapChain->getImages().at(imageIndex);
            auto wrapImage = core::Image::createShared(image);

            auto *swapChainRenderTarget = storage.getSwapChainTexture(id, imageIndex);
            swapChainRenderTarget->destroyVkImageView();
            swapChainRenderTarget->destroyVkImage();

            swapChainRenderTarget->resetVkImage(wrapImage, swapChain->getExtent());
            swapChainRenderTarget->createVkImageView();

            if (textureDescription.getFinalLayout() == VK_IMAGE_LAYOUT_UNDEFINED && textureDescription.getInitialLayout() == VK_IMAGE_LAYOUT_UNDEFINED)
                continue;

            // Help to translate image from VK_IMAGE_LAYOUT_UNDEFINED

            VkImageSubresourceRange subresourceRange{};
            subresourceRange.aspectMask = utilities::ImageUtilities::getAspectBasedOnFormat(textureDescription.getFormat());
            subresourceRange.baseMipLevel = 0;
            subresourceRange.levelCount = 1;
            subresourceRange.baseArrayLayer = 0;
            subresourceRange.layerCount = 1;

            VkPipelineStageFlags2 srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            VkAccessFlags2 srcAccessMask = 0;
            VkPipelineStageFlags2 dstStageMask;
            VkAccessFlags2 dstAccessMask;

            const VkImageLayout bootstrapLayout = chooseBootstrapLayout(textureDescription.getInitialLayout(), textureDescription.getFinalLayout());

            chooseDstSync(bootstrapLayout, dstStageMask, dstAccessMask);

            auto barrier = utilities::ImageUtilities::insertImageMemoryBarrier(*wrapImage, srcAccessMask, dstAccessMask, VK_IMAGE_LAYOUT_UNDEFINED,
                                                                               bootstrapLayout, srcStageMask,
                                                                               dstStageMask, subresourceRange);

            barriers.push_back(barrier);
        }
    }

    return barriers;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
