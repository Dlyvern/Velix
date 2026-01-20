#include "Engine/Render/RenderGraphPassRecourceCompiler.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

RenderGraphPassResourceCompiler::RenderGraphPassResourceCompiler(VkDevice device, VkPhysicalDevice physicalDevice, core::SwapChain::SharedPtr swapChain) : m_device(device), m_physicalDevice(physicalDevice), m_swapChain(swapChain)
{
}

void RenderGraphPassResourceCompiler::compile(RenderGraphPassRecourceBuilder &builder, RenderGraphPassResourceHash &storage)
{
    compileTextures(builder, storage);
    compileRenderPasses(builder, storage);
    compileFramebuffers(builder, storage);
    compileGraphicsPipelines(builder, storage);
}

void RenderGraphPassResourceCompiler::compileGraphicsPipelines(RenderGraphPassRecourceBuilder &builder, RenderGraphPassResourceHash &storage)
{
    for (auto [hash, graphicsPipelineDescription] : builder.getGraphicsPipelineHashes())
    {
        VkPipelineVertexInputStateCreateInfo vertexInputStateCI{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInputStateCI.vertexBindingDescriptionCount = static_cast<uint32_t>(graphicsPipelineDescription.vertexBindingDescriptions.size());
        vertexInputStateCI.pVertexBindingDescriptions = graphicsPipelineDescription.vertexBindingDescriptions.data();
        vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(graphicsPipelineDescription.vertexAttributeDescriptions.size());
        vertexInputStateCI.pVertexAttributeDescriptions = graphicsPipelineDescription.vertexAttributeDescriptions.data();

        graphicsPipelineDescription.viewportState.pViewports = &graphicsPipelineDescription.viewport;
        graphicsPipelineDescription.viewportState.pScissors = &graphicsPipelineDescription.scissor;

        if (!graphicsPipelineDescription.colorBlendingAttachments.empty())
        {
            graphicsPipelineDescription.colorBlending.attachmentCount = static_cast<uint32_t>(graphicsPipelineDescription.colorBlendingAttachments.size());
            graphicsPipelineDescription.colorBlending.pAttachments = graphicsPipelineDescription.colorBlendingAttachments.data();
        }

        graphicsPipelineDescription.dynamicState.dynamicStateCount = (uint32_t)graphicsPipelineDescription.dynamicStates.size();
        graphicsPipelineDescription.dynamicState.pDynamicStates = graphicsPipelineDescription.dynamicStates.data();

        auto shaderStages = graphicsPipelineDescription.shader->getShaderStages();

        VkRenderPass renderPass;

        if (!graphicsPipelineDescription.renderPass)
            renderPass = storage.getRenderPass(graphicsPipelineDescription.renderPassHash)->vk();
        else
            renderPass = graphicsPipelineDescription.renderPass;

        auto graphicsPipeline = std::make_shared<core::GraphicsPipeline>(m_device, renderPass, shaderStages.data(),
                                                                         static_cast<uint32_t>(graphicsPipelineDescription.shader->getShaderStages().size()), graphicsPipelineDescription.layout, graphicsPipelineDescription.dynamicState, graphicsPipelineDescription.colorBlending,
                                                                         graphicsPipelineDescription.multisampling, graphicsPipelineDescription.rasterizer, graphicsPipelineDescription.viewportState, graphicsPipelineDescription.inputAssembly,
                                                                         vertexInputStateCI, graphicsPipelineDescription.subpass, graphicsPipelineDescription.depthStencil);

        storage.addGraphicsPipeline(hash, graphicsPipeline);
    }
}

void RenderGraphPassResourceCompiler::compileRenderPasses(RenderGraphPassRecourceBuilder &builder, RenderGraphPassResourceHash &storage)
{
    for (const auto &[hash, renderPassDescription] : builder.getRenderPassHashes())
    {
        std::vector<VkSubpassDescription> subpassDescriptions;

        for (const auto &subpassDescription : renderPassDescription.subpassDescriptions)
        {
            VkSubpassDescription description{};
            description.pipelineBindPoint = subpassDescription.pipelineBindPoint;
            description.colorAttachmentCount = subpassDescription.colorAttachmentCount;
            description.pColorAttachments = subpassDescription.colorAttachments.data();
            description.pDepthStencilAttachment = subpassDescription.depthStencilAttachments.data();

            subpassDescriptions.push_back(description);
        }

        auto renderPass = core::RenderPass::create(renderPassDescription.attachments, subpassDescriptions,
                                                   renderPassDescription.subpassDependencies);

        storage.addRenderPass(hash, renderPass);
    }
}

void RenderGraphPassResourceCompiler::compileTextures(RenderGraphPassRecourceBuilder &builder, RenderGraphPassResourceHash &storage)
{
    const auto textureHashes = builder.getTextureHashes();

    for (const auto &[hash, textureDescription] : textureHashes)
    {
        if (textureDescription.source == RenderGraphPassResourceTypes::TextureDescription::FormatSource::Swapchain)
        {
            std::cout << "SWAPCHAIN_TEXTURE" << std::endl;

            std::size_t copyHash = hash;

            int index = 0;

            for (const auto &image : m_swapChain.lock()->getImages())
            {
                auto wrapImage = core::Image::createShared(image);

                auto texture = std::make_shared<RenderTarget>(m_device, m_physicalDevice, m_swapChain.lock()->getImageFormat(),
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

        if (textureDescription.size.type == RenderGraphPassResourceTypes::SizeClass::SwapchainRelative)
            size = m_swapChain.lock()->getExtent();
        else
            size = {textureDescription.size.width, textureDescription.size.height};

        auto image = core::Image::createShared(size.width, size.height,
                                               textureDescription.usage, textureDescription.memoryFlags, textureDescription.format, textureDescription.tiling);

        auto texture = std::make_shared<RenderTarget>(m_device, m_physicalDevice, textureDescription.format, textureDescription.aspect, image);
        texture->setSampler(textureDescription.sampler);
        storage.addTexture(hash, texture);
    }
}

void RenderGraphPassResourceCompiler::compileFramebuffers(RenderGraphPassRecourceBuilder &builder, RenderGraphPassResourceHash &storage)
{
    for (const auto &[hash, framebufferDescription] : builder.getFramebufferHashes())
    {
        std::vector<VkImageView> attachments;

        for (const auto &attachmentHash : framebufferDescription.attachmentsHash)
        {
            auto texture = storage.getTexture(attachmentHash);

            if (!texture)
            {
                std::cerr << "Failed to find framebuffer attachment" << std::endl;
                continue;
            }

            attachments.push_back(texture->vkImageView());
        }

        if (attachments.empty())
        {
            std::cerr << "Attachments for framebuffer are empty" << std::endl;
        }

        VkExtent2D size;

        if (framebufferDescription.size.type == RenderGraphPassResourceTypes::SizeClass::SwapchainRelative)
            size = m_swapChain.lock()->getExtent();
        else
            size = {framebufferDescription.size.width, framebufferDescription.size.height};

        core::RenderPass::SharedPtr renderPass{nullptr};

        if (!framebufferDescription.renderPass)
            renderPass = storage.getRenderPass(framebufferDescription.renderPassHash);
        else
            renderPass = framebufferDescription.renderPass;

        auto framebuffer = std::make_shared<core::Framebuffer>(m_device, attachments, renderPass,
                                                               VkExtent2D{size.width, size.height}, framebufferDescription.layers);

        storage.addFramebuffer(hash, framebuffer);
    }
}

void RenderGraphPassResourceCompiler::onSwapChainResize(const RenderGraphPassRecourceBuilder &builder, RenderGraphPassResourceHash &storage)
{
    for (const auto &[imageIndex, swapChainRelatedHash] : m_swapChainHash)
    {
        for (const auto &swapHash : swapChainRelatedHash)
        {
            auto image = m_swapChain.lock()->getImages()[imageIndex];

            auto texture = storage.getTexture(swapHash);

            if (!texture)
            {
                std::cerr << "Failed to find a swap chain related texture" << std::endl;
                continue;
            }

            texture->destroyVkImageView();

            auto wrapImage = core::Image::createShared(image);

            texture->resetVkImage(wrapImage);

            texture->createVkImageView();
        }
    }

    for (const auto &[hash, textureDescription] : builder.getTextureHashes())
    {
        if (textureDescription.size.type != RenderGraphPassResourceTypes::SizeClass::SwapchainRelative)
            continue;

        if (textureDescription.source == RenderGraphPassResourceTypes::TextureDescription::FormatSource::Swapchain)
            continue;

        auto texture = storage.getTexture(hash);

        if (!texture)
        {
            std::cerr << "Failed to find non-swapchain texture hash " << std::endl;
            continue;
        }

        texture->destroyVkImageView();
        texture->destroyVkImage();

        texture->createVkImage(m_swapChain.lock()->getExtent(), textureDescription.usage,
                               textureDescription.memoryFlags, textureDescription.format,
                               textureDescription.tiling);

        texture->createVkImageView();
    }

    for (const auto &[hash, framebufferDescription] : builder.getFramebufferHashes())
    {
        if (framebufferDescription.size.type != RenderGraphPassResourceTypes::SizeClass::SwapchainRelative)
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

        if (!framebuffer)
        {
            std::cerr << "Failed to find framebuffer" << std::endl;
            continue;
        }

        framebuffer->resize(m_swapChain.lock()->getExtent(), attachments);
    }
}

ELIX_NESTED_NAMESPACE_END