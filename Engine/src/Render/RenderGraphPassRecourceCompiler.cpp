#include "Engine/Render/RenderGraphPassRecourceCompiler.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

RenderGraphPassResourceCompiler::RenderGraphPassResourceCompiler(VkDevice device, VkPhysicalDevice physicalDevice, core::SwapChain::SharedPtr swapChain) : 
m_device(device), m_physicalDevice(physicalDevice), m_swapChain(swapChain)
{
    
}

void RenderGraphPassResourceCompiler::compile(RenderGraphPassRecourceBuilder& builder, RenderGraphPassResourceHash& storage)
{
    compileTextures(builder, storage);
    compileFramebuffers(builder, storage);
    compileRenderPasses(builder, storage);
}

void RenderGraphPassResourceCompiler::compileRenderPasses(RenderGraphPassRecourceBuilder& builder, RenderGraphPassResourceHash& storage)
{
    for(const auto& [hash, renderPassDescription] : builder.getRenderPassHashes())
    {
        std::vector<VkAttachmentReference> colorAttachments;
        
        for(const auto& att : renderPassDescription.colorAttachments)
            colorAttachments.push_back(att.colorAttachmentReference);
        
        VkSubpassDescription subpassDescription{};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.pColorAttachments = colorAttachments.data();
        subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        subpassDescription.pDepthStencilAttachment = renderPassDescription.depthAttachments.data();

        auto renderPass = core::RenderPass::create(m_device, renderPassDescription.attachments, {subpassDescription}, 
        renderPassDescription.subpassDependencies);

        storage.addRenderPass(hash, renderPass);
        // subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        // subpass.colorAttachmentCount = 1;
        // subpass.pColorAttachments = &colorAttachmentReference;
        // subpass.pDepthStencilAttachment = &depthAttachmentReference;

        // auto renderPass = core::RenderPass::create(m_device, renderPassDescription.attachments, 
        // renderPassDescription.subpassDescriptions, renderPassDescription.subpassDependencies);

        // storage.addRenderPass(hash, renderPass);
        // VkAttachmentDescription colorAttachment{};
        // colorAttachment.format = m_swapchain->getImageFormat();
        // colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        // colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        // colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        // colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        // colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        // colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        // VkAttachmentDescription depthAttachment{};
        // depthAttachment.format = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());
        // depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        // depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        // depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        // depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        // depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        // depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // VkAttachmentReference colorAttachmentReference{};
        // colorAttachmentReference.attachment = 0;
        // colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // VkAttachmentReference depthAttachmentReference{};
        // depthAttachmentReference.attachment = 1;
        // depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // VkSubpassDescription subpass{};
        // subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        // subpass.colorAttachmentCount = 1;
        // subpass.pColorAttachments = &colorAttachmentReference;
        // subpass.pDepthStencilAttachment = &depthAttachmentReference;

        // std::vector<VkSubpassDependency> dependency;
        // dependency.resize(1);
        // dependency[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        // dependency[0].dstSubpass = 0;
        // dependency[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        // dependency[0].srcAccessMask = 0;
        // dependency[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        // dependency[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        // std::vector<VkAttachmentDescription> attachments{colorAttachment, depthAttachment};

        // m_renderPass = core::RenderPass::create(m_device, attachments, {subpass}, dependency);
    }
}

void RenderGraphPassResourceCompiler::compileTextures(RenderGraphPassRecourceBuilder& builder, RenderGraphPassResourceHash& storage)
{
    const auto textureHashes = builder.getTextureHashes();

    for(const auto& [hash, textureDescription] : textureHashes)
    {
        if(textureDescription.source == RenderGraphPassResourceTypes::TextureDescription::FormatSource::Swapchain)
        {
            std::cout << "SWAPCHAIN_TEXTURE" << std::endl;

            std::size_t copyHash = hash;

            int index = 0;
            
            for(const auto& image : m_swapChain.lock()->getImages())
            {
                auto wrapImage = core::Image<core::ImageNoDelete>::wrap(m_device, image);

                auto texture = std::make_shared<core::Texture<core::ImageNoDelete>>(m_device, m_physicalDevice, m_swapChain.lock()->getImageFormat(),
                textureDescription.aspect, wrapImage);

                storage.addTexture(copyHash, texture);
                builder.forceTextureCache(copyHash, textureDescription);

                m_swapChainHash[index].push_back(copyHash);
                ++copyHash;
                ++index;
            }

            continue;
        }

        VkExtent2D size;

        if(textureDescription.size.type == RenderGraphPassResourceTypes::SizeClass::SwapchainRelative)
            size = m_swapChain.lock()->getExtent();
        else
            size = {textureDescription.size.width, textureDescription.size.height};

        auto image = core::Image<core::ImageNoDelete>::createCustom<core::ImageNoDelete>(m_device, m_physicalDevice, size.width, size.height,
        textureDescription.usage, textureDescription.properties, textureDescription.format, textureDescription.tiling);

        auto texture = std::make_shared<core::Texture<core::ImageNoDelete>>(m_device, m_physicalDevice, textureDescription.format, textureDescription.aspect, image);

        storage.addTexture(hash, texture);
    }
}

void RenderGraphPassResourceCompiler::compileFramebuffers(RenderGraphPassRecourceBuilder& builder, RenderGraphPassResourceHash& storage)
{
    for(const auto& [hash, framebufferDescription] : builder.getFramebufferHashes())
    {
        std::vector<VkImageView> attachments;

        for(const auto& attachmentHash : framebufferDescription.attachmentsHash)
        {
            auto texture = storage.getTexture(attachmentHash);

            if(!texture)
            {
                std::cerr << "Failed to find framebuffer attachment" << std::endl;
                continue;
            }

            attachments.push_back(texture->vkImageView());
        }

        if(attachments.empty())
        {
            std::cerr << "Attachments for framebuffer are empty" << std::endl;
        }

        VkExtent2D size;

        if(framebufferDescription.size.type == RenderGraphPassResourceTypes::SizeClass::SwapchainRelative)
            size = m_swapChain.lock()->getExtent();
        else
            size = {framebufferDescription.size.width, framebufferDescription.size.height};

        auto framebuffer = std::make_shared<core::Framebuffer>(m_device, attachments, framebufferDescription.renderPass, 
        VkExtent2D{size.width, size.height}, framebufferDescription.layers);

        storage.addFramebuffer(hash, framebuffer);
    }

}

void RenderGraphPassResourceCompiler::onSwapChainResize(const RenderGraphPassRecourceBuilder& builder, RenderGraphPassResourceHash& storage)
{
    for(const auto& [imageIndex, swapChainRelatedHash] : m_swapChainHash)
    {
        for(const auto& swapHash : swapChainRelatedHash)
        {
            auto image = m_swapChain.lock()->getImages()[imageIndex];

            auto texture = storage.getTexture(swapHash);

            if(!texture)
            {
                std::cerr << "Failed to find a swap chain related texture" << std::endl;
                continue;
            }

            texture->destroyVkImageView();

            auto wrapImage = core::Image<core::ImageNoDelete>::wrap(m_device, image);

            texture->resetVkImage(wrapImage);

            texture->createVkImageView();
        }
    }

    for(const auto& [hash, textureDescription] : builder.getTextureHashes())
    {
        if(textureDescription.size.type != RenderGraphPassResourceTypes::SizeClass::SwapchainRelative)
            continue;

        if(textureDescription.source == RenderGraphPassResourceTypes::TextureDescription::FormatSource::Swapchain)
            continue;

        auto texture =  storage.getTexture(hash);

        if(!texture)
        {
            std::cerr << "Failed to find non-swapchain texture hash " << std::endl;
            continue;
        }

        texture->destroyVkImageView();
        texture->destroyVkImage();

        texture->createVkImage(m_swapChain.lock()->getExtent(), textureDescription.usage,
                                textureDescription.properties, textureDescription.format,
                                textureDescription.tiling);

        texture->createVkImageView();
    }

    for(const auto& [hash, framebufferDescription] : builder.getFramebufferHashes())
    {
        if(framebufferDescription.size.type != RenderGraphPassResourceTypes::SizeClass::SwapchainRelative)
            continue;

        std::vector<VkImageView> attachments;

        for (auto attachementHash : framebufferDescription.attachmentsHash)
        {
            auto texture = storage.getTexture(attachementHash);

            if (!texture)
            {
                std::cerr << "failed to find texture to resize framebuffer" << std::endl;
                continue;
            }

            attachments.push_back(texture->vkImageView());
        }

        auto framebuffer = storage.getFramebuffer(hash);

        if(!framebuffer)
        {
            std::cerr << "Failed to find framebuffer" << std::endl;
            continue;
        }

        framebuffer->resize(m_swapChain.lock()->getExtent(), attachments);
    }
}

ELIX_NESTED_NAMESPACE_END