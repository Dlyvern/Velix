#include "Engine/Render/GraphPasses/RTGIDenoiseRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Shaders/PushConstant.hpp"

#include <algorithm>
#include <glm/gtc/matrix_inverse.hpp>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

RTGIDenoiseRenderGraphPass::RTGIDenoiseRenderGraphPass(
    std::vector<RGPResourceHandler> &giHandlers,
    std::vector<RGPResourceHandler> &normalHandlers,
    RGPResourceHandler              &depthHandler)
    : m_giHandlers(giHandlers),
      m_normalHandlers(normalHandlers),
      m_depthHandler(depthHandler)
{
    setDebugName("RT GI denoise render graph pass");
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    outputs.color.setOwner(this);
}

void RTGIDenoiseRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        builder.read(m_giHandlers[i], RGPTextureUsage::SAMPLED);
        builder.read(m_normalHandlers[i], RGPTextureUsage::SAMPLED);
    }
    builder.read(m_depthHandler, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT_STORAGE};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_GENERAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this] { return m_extent; });

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_RT_GI_DENOISE_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT_STORAGE);
    }
    outputs.color.set(m_outputHandlers);

    auto device = core::VulkanContext::getContext()->getDevice();

    auto makeBinding = [](uint32_t binding, VkDescriptorType type) -> VkDescriptorSetLayoutBinding
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding         = binding;
        b.descriptorType  = type;
        b.descriptorCount = 1;
        b.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        return b;
    };

    // Set 0: b0=input, b1=gbuf normal, b2=depth, b3=output storage image
    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(
        device,
        std::vector<VkDescriptorSetLayoutBinding>{
            makeBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            makeBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            makeBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            makeBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout},
        std::vector<VkPushConstantRange>{
            PushConstant<RTGIDenoisePC>::getRange(VK_SHADER_STAGE_COMPUTE_BIT)});

    m_sampler      = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_depthSampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    createComputePipeline();
}

void RTGIDenoiseRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputRenderTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    const auto *depthTarget = storage.getTexture(m_depthHandler);
    const auto device = core::VulkanContext::getContext()->getDevice();
    const auto pool   = core::VulkanContext::getContext()->getPersistentDescriptorPool();

    // Recreate ping-pong images whenever compile() runs (extent may have changed).
    destroyPingPongImages();
    createPingPongImages();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        const auto *giTarget     = storage.getTexture(m_giHandlers[i]);
        const auto *normalTarget = storage.getTexture(m_normalHandlers[i]);
        const auto *outputTarget = storage.getTexture(m_outputHandlers[i]);
        m_outputRenderTargets[i] = outputTarget;

        VkImageView pingView = m_pingPong[i].pingView;
        VkImageView pongView = m_pingPong[i].pongView;

        if (!m_descriptorSetsInitialized)
        {
            // pass0: rawGI → ping
            m_descriptorSets[i][0] = DescriptorSetBuilder::begin()
                .addImage(giTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(normalTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .addImage(depthTarget->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 2)
                .addStorageImage(pingView, VK_IMAGE_LAYOUT_GENERAL, 3)
                .build(device, pool, m_descriptorSetLayout);

            // pass1: ping → pong
            m_descriptorSets[i][1] = DescriptorSetBuilder::begin()
                .addImage(pingView, m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(normalTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .addImage(depthTarget->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 2)
                .addStorageImage(pongView, VK_IMAGE_LAYOUT_GENERAL, 3)
                .build(device, pool, m_descriptorSetLayout);

            // pass2: pong → final output (graph resource)
            m_descriptorSets[i][2] = DescriptorSetBuilder::begin()
                .addImage(pongView, m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(normalTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .addImage(depthTarget->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 2)
                .addStorageImage(outputTarget->vkImageView(), VK_IMAGE_LAYOUT_GENERAL, 3)
                .build(device, pool, m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(giTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(normalTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .addImage(depthTarget->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 2)
                .addStorageImage(pingView, VK_IMAGE_LAYOUT_GENERAL, 3)
                .update(device, m_descriptorSets[i][0]);

            DescriptorSetBuilder::begin()
                .addImage(pingView, m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(normalTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .addImage(depthTarget->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 2)
                .addStorageImage(pongView, VK_IMAGE_LAYOUT_GENERAL, 3)
                .update(device, m_descriptorSets[i][1]);

            DescriptorSetBuilder::begin()
                .addImage(pongView, m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(normalTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .addImage(depthTarget->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 2)
                .addStorageImage(outputTarget->vkImageView(), VK_IMAGE_LAYOUT_GENERAL, 3)
                .update(device, m_descriptorSets[i][2]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void RTGIDenoiseRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                        const RenderGraphPassPerFrameData  &data,
                                        const RenderGraphPassContext       &renderContext)
{
    if (m_computePipeline == VK_NULL_HANDLE)
        return;

    const uint32_t idx = renderContext.currentImageIndex;
    const auto *outputTarget = m_outputRenderTargets[idx];
    if (!outputTarget || m_extent.width == 0u || m_extent.height == 0u)
        return;

    if (data.drawBatches.empty())
    {
        VkClearColorValue clear{};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(commandBuffer->vk(), outputTarget->getImage()->vk(),
                             VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
        return;
    }

    const auto &settings = RenderQualitySettings::getInstance();
    const bool denoiseEnabled = settings.enableRTGI && settings.enableRTGIDenoiser;

    RTGIDenoisePC pc{};
    pc.texelW       = 1.0f / static_cast<float>(m_extent.width);
    pc.texelH       = 1.0f / static_cast<float>(m_extent.height);
    pc.normalSigma  = 0.08f;  // tighter: colored radiance needs sharper edge preservation
    pc.depthSigma   = 0.3f;   // tighter: prevent color bleeding across depth discontinuities
    pc.enabled      = denoiseEnabled ? 1.0f : 0.0f;
    pc.invProjection = glm::inverse(data.projection);

    const uint32_t gx = (m_extent.width  + 7u) / 8u;
    const uint32_t gy = (m_extent.height + 7u) / 8u;

    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);

    const float stepWidths[3] = {1.0f, 2.0f, 4.0f};
    for (int pass = 0; pass < 3; ++pass)
    {
        pc.stepWidth = stepWidths[pass];

        if (pass > 0)
        {
            // Barrier: wait for previous write, then transition ping or pong from GENERAL→SHADER_READ
            VkImage srcImage = (pass == 1) ? m_pingPong[idx].pingImage->vk()
                                           : m_pingPong[idx].pongImage->vk();
            transitionPingPong(commandBuffer->vk(), srcImage,
                               VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            // Transition the write target back to GENERAL
            VkImage dstImage = (pass == 1) ? m_pingPong[idx].pongImage->vk()
                                           : outputTarget->getImage()->vk();
            if (pass == 1)
                transitionPingPong(commandBuffer->vk(), dstImage,
                                   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            // pass 2 writes the graph output — the render graph already put it in GENERAL
        }
        else
        {
            // Transition both ping and pong to GENERAL before first pass
            transitionPingPong(commandBuffer->vk(), m_pingPong[idx].pingImage->vk(),
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            transitionPingPong(commandBuffer->vk(), m_pingPong[idx].pongImage->vk(),
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        }

        vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_pipelineLayout, 0, 1,
                                &m_descriptorSets[idx][pass], 0, nullptr);
        vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(commandBuffer->vk(), gx, gy, 1u);
    }

    // Transition ping and pong back to undefined so future compiles start clean
    transitionPingPong(commandBuffer->vk(), m_pingPong[idx].pongImage->vk(),
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED);
    transitionPingPong(commandBuffer->vk(), m_pingPong[idx].pingImage->vk(),
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED);
}

std::vector<IRenderGraphPass::RenderPassExecution>
RTGIDenoiseRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.mode             = IRenderGraphPass::ExecutionMode::Direct;
    exec.useDepth         = false;
    exec.depthFormat      = VK_FORMAT_UNDEFINED;
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;
    exec.targets[m_outputHandlers[renderContext.currentImageIndex]] = m_outputRenderTargets[renderContext.currentImageIndex];
    return {exec};
}

void RTGIDenoiseRenderGraphPass::setExtent(VkExtent2D extent)
{
    if (m_extent.width == extent.width && m_extent.height == extent.height)
        return;
    m_extent = extent;
    requestRecompilation();
}

void RTGIDenoiseRenderGraphPass::cleanup()
{
    destroyComputePipeline();
    destroyPingPongImages();
}

void RTGIDenoiseRenderGraphPass::freeResources()
{
    m_outputRenderTargets.clear();
    for (auto &arr : m_descriptorSets)
        arr.fill(VK_NULL_HANDLE);
    m_descriptorSetsInitialized = false;
    outputs.color.set(MultiHandle{});
}

void RTGIDenoiseRenderGraphPass::createComputePipeline()
{
    destroyComputePipeline();

    auto context = core::VulkanContext::getContext();
    if (!context)
        return;

    m_computeShader.loadFromFile("./resources/shaders/rt_gi_denoise.comp.spv", core::ShaderStage::COMPUTE);

    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage  = m_computeShader.getInfo();
    ci.layout = m_pipelineLayout;

    if (vkCreateComputePipelines(context->getDevice(), VK_NULL_HANDLE, 1, &ci, nullptr, &m_computePipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create RT GI denoise compute pipeline");
}

void RTGIDenoiseRenderGraphPass::destroyComputePipeline()
{
    auto context = core::VulkanContext::getContext();
    if (context && m_computePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(context->getDevice(), m_computePipeline, nullptr);
        m_computePipeline = VK_NULL_HANDLE;
    }
    m_computeShader.destroyVk();
}

void RTGIDenoiseRenderGraphPass::createPingPongImages()
{
    auto context = core::VulkanContext::getContext();
    if (!context || m_extent.width == 0 || m_extent.height == 0)
        return;

    const uint32_t imageCount = context->getSwapchain()->getImageCount();
    m_pingPong.resize(imageCount);

    const auto device = context->getDevice();
    const VkImageUsageFlags usage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_pingPong[i].pingImage = std::make_shared<core::Image>(
            m_extent, usage, core::memory::MemoryUsage::GPU_ONLY, m_format);
        m_pingPong[i].pongImage = std::make_shared<core::Image>(
            m_extent, usage, core::memory::MemoryUsage::GPU_ONLY, m_format);

        auto createView = [&](VkImage image, VkImageView &view)
        {
            VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            info.image    = image;
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format   = m_format;
            info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (vkCreateImageView(device, &info, nullptr, &view) != VK_SUCCESS)
                throw std::runtime_error("Failed to create GI ping-pong image view");
        };

        createView(m_pingPong[i].pingImage->vk(), m_pingPong[i].pingView);
        createView(m_pingPong[i].pongImage->vk(), m_pingPong[i].pongView);
    }
}

void RTGIDenoiseRenderGraphPass::destroyPingPongImages()
{
    auto context = core::VulkanContext::getContext();
    if (!context)
    {
        m_pingPong.clear();
        return;
    }

    const auto device = context->getDevice();
    for (auto &pp : m_pingPong)
    {
        if (pp.pongView) { vkDestroyImageView(device, pp.pongView, nullptr); pp.pongView = VK_NULL_HANDLE; }
        if (pp.pingView) { vkDestroyImageView(device, pp.pingView, nullptr); pp.pingView = VK_NULL_HANDLE; }
        pp.pongImage.reset();
        pp.pingImage.reset();
    }
    m_pingPong.clear();
}

void RTGIDenoiseRenderGraphPass::transitionPingPong(VkCommandBuffer cmd, VkImage image,
                                                    VkImageLayout from, VkImageLayout to)
{
    if (from == to)
        return;

    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.image               = image;
    barrier.oldLayout           = from;
    barrier.newLayout           = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    if (from == VK_IMAGE_LAYOUT_GENERAL)
    {
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    }
    else
    {
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.srcAccessMask = 0;
    }

    if (to == VK_IMAGE_LAYOUT_GENERAL)
    {
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    }
    else if (to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    }
    else
    {
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        barrier.dstAccessMask = 0;
    }

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
