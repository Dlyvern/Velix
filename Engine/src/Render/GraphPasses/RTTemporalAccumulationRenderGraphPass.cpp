#include "Engine/Render/GraphPasses/RTTemporalAccumulationRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Shaders/PushConstant.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

RTTemporalAccumulationRenderGraphPass::RTTemporalAccumulationRenderGraphPass(
    std::vector<RGPResourceHandler> &inputHandlers,
    RGPResourceHandler              &depthHandler,
    std::string                      debugName)
    : m_inputHandlers(inputHandlers),
      m_depthHandler(depthHandler)
{
    setDebugName(std::move(debugName));
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    outputs.color.setOwner(this);
}

void RTTemporalAccumulationRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    for (uint32_t i = 0; i < imageCount; ++i)
        builder.read(m_inputHandlers[i], RGPTextureUsage::SAMPLED);
    builder.read(m_depthHandler, RGPTextureUsage::SAMPLED);

    // Output: accumulated temporally-blended result (same format as input).
    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT_STORAGE};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_GENERAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this] { return m_extent; });

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_RT_TEMPORAL_" + getDebugName() + "_" + std::to_string(i) + "__");
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

    // Set 0:
    //   0 = uCurrentFrame  (sampler2D  — denoised RT input)
    //   1 = uDepth         (sampler2D  — depth)
    //   2 = uHistory       (sampler2D  — previous accumulated, ping-pong read)
    //   3 = uOutput        (storage    — accumulated result for downstream)
    //   4 = uHistoryOut    (storage    — write-back to history, ping-pong write)
    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(
        device,
        std::vector<VkDescriptorSetLayoutBinding>{
            makeBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            makeBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            makeBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            makeBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            makeBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout},
        std::vector<VkPushConstantRange>{
            PushConstant<TemporalPC>::getRange(VK_SHADER_STAGE_COMPUTE_BIT)});

    m_sampler     = core::Sampler::createShared(VK_FILTER_LINEAR,  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_depthSampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    createComputePipeline();
}

void RTTemporalAccumulationRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputRenderTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    const auto *depthTarget = storage.getTexture(m_depthHandler);
    const auto  device      = core::VulkanContext::getContext()->getDevice();
    const auto  pool        = core::VulkanContext::getContext()->getPersistentDescriptorPool();

    // History images are recreated whenever the extent changes.
    destroyHistoryImages();
    createHistoryImages(); // also resets m_pingPong and m_historyLayout

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        const auto *inputTarget  = storage.getTexture(m_inputHandlers[i]);
        const auto *outputTarget = storage.getTexture(m_outputHandlers[i]);
        m_outputRenderTargets[i] = outputTarget;

        // Two descriptor sets per swapchain image — one per ping-pong state.
        // State 0: read history[i][0], write history[i][1] + output[i]
        // State 1: read history[i][1], write history[i][0] + output[i]
        for (uint32_t p = 0; p < 2; ++p)
        {
            uint32_t readIdx  = p;
            uint32_t writeIdx = 1u - p;

            VkImageView histReadView  = m_historyPairs[i].views[readIdx];
            VkImageView histWriteView = m_historyPairs[i].views[writeIdx];

            if (!m_descriptorSetsInitialized)
            {
                m_descriptorSets[i][p] = DescriptorSetBuilder::begin()
                    .addImage(inputTarget->vkImageView(),  m_sampler,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                    .addImage(depthTarget->vkImageView(),  m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                    .addImage(histReadView,                m_sampler,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                    .addStorageImage(outputTarget->vkImageView(), VK_IMAGE_LAYOUT_GENERAL, 3)
                    .addStorageImage(histWriteView, VK_IMAGE_LAYOUT_GENERAL, 4)
                    .build(device, pool, m_descriptorSetLayout);
            }
            else
            {
                DescriptorSetBuilder::begin()
                    .addImage(inputTarget->vkImageView(),  m_sampler,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                    .addImage(depthTarget->vkImageView(),  m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                    .addImage(histReadView,                m_sampler,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                    .addStorageImage(outputTarget->vkImageView(), VK_IMAGE_LAYOUT_GENERAL, 3)
                    .addStorageImage(histWriteView, VK_IMAGE_LAYOUT_GENERAL, 4)
                    .update(device, m_descriptorSets[i][p]);
            }
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void RTTemporalAccumulationRenderGraphPass::record(core::CommandBuffer::SharedPtr      commandBuffer,
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

    const uint32_t p         = m_pingPong[idx];          // current ping-pong state
    const uint32_t readIdx   = p;
    const uint32_t writeIdx  = 1u - p;

    VkImage histReadImage  = m_historyPairs[idx].images[readIdx]->vk();
    VkImage histWriteImage = m_historyPairs[idx].images[writeIdx]->vk();

    // ---- Prepare history layouts ------------------------------------------------

    // If the read-history has never been written, clear it to black first so we
    // don't blend against garbage on the first frame.
    auto &readLayout  = m_historyLayout[idx][readIdx];
    auto &writeLayout = m_historyLayout[idx][writeIdx];

    if (readLayout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        // UNDEFINED → GENERAL → clear → SHADER_READ_ONLY
        transitionHistory(commandBuffer->vk(), histReadImage,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        VkClearColorValue black{};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(commandBuffer->vk(), histReadImage,
                             VK_IMAGE_LAYOUT_GENERAL, &black, 1, &range);
        transitionHistory(commandBuffer->vk(), histReadImage,
                          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    // Read-history is now guaranteed SHADER_READ_ONLY_OPTIMAL.

    // Write-history: move to GENERAL for the compute dispatch.
    transitionHistory(commandBuffer->vk(), histWriteImage, writeLayout, VK_IMAGE_LAYOUT_GENERAL);
    writeLayout = VK_IMAGE_LAYOUT_GENERAL;

    // ---- Build push constants ---------------------------------------------------

    const glm::mat4 currentVP   = data.projection * data.view;
    const glm::mat4 invViewProj = glm::inverse(currentVP);

    TemporalPC pc{};
    pc.invViewProj        = invViewProj;
    pc.prevViewProjection = m_prevViewProjection;

    // ---- Dispatch ---------------------------------------------------------------

    const uint32_t gx = (m_extent.width  + 7u) / 8u;
    const uint32_t gy = (m_extent.height + 7u) / 8u;

    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_pipelineLayout, 0, 1, &m_descriptorSets[idx][p], 0, nullptr);
    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    vkCmdDispatch(commandBuffer->vk(), gx, gy, 1u);

    // ---- Transition write-history to SHADER_READ_ONLY for the next frame ---------
    transitionHistory(commandBuffer->vk(), histWriteImage,
                      VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    writeLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // ---- Flip ping-pong for this swapchain image --------------------------------
    m_pingPong[idx] = 1u - p;

    // ---- Store current VP for next frame's reprojection -------------------------
    m_prevViewProjection = currentVP;
}

std::vector<IRenderGraphPass::RenderPassExecution>
RTTemporalAccumulationRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.mode              = IRenderGraphPass::ExecutionMode::Direct;
    exec.useDepth          = false;
    exec.depthFormat       = VK_FORMAT_UNDEFINED;
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;
    exec.targets[m_outputHandlers[renderContext.currentImageIndex]] =
        m_outputRenderTargets[renderContext.currentImageIndex];
    return {exec};
}

void RTTemporalAccumulationRenderGraphPass::setExtent(VkExtent2D extent)
{
    if (m_extent.width == extent.width && m_extent.height == extent.height)
        return;
    m_extent             = extent;
    m_prevViewProjection = glm::mat4{1.0f}; // invalidate history on resize
    requestRecompilation();
}

void RTTemporalAccumulationRenderGraphPass::cleanup()
{
    destroyComputePipeline();
    destroyHistoryImages();
}

void RTTemporalAccumulationRenderGraphPass::freeResources()
{
    m_outputRenderTargets.clear();
    for (auto &arr : m_descriptorSets)
        arr.fill(VK_NULL_HANDLE);
    m_descriptorSetsInitialized = false;
    outputs.color.set(MultiHandle{});
}

// ---- Private helpers ---------------------------------------------------------

void RTTemporalAccumulationRenderGraphPass::createHistoryImages()
{
    auto context = core::VulkanContext::getContext();
    if (!context || m_extent.width == 0u || m_extent.height == 0u)
        return;

    const uint32_t imageCount = context->getSwapchain()->getImageCount();
    m_historyPairs.resize(imageCount);
    m_pingPong.assign(imageCount, 0u);
    m_historyLayout.assign(imageCount, {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED});

    const auto device = context->getDevice();
    const VkImageUsageFlags usage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        for (uint32_t j = 0; j < 2u; ++j)
        {
            m_historyPairs[i].images[j] = std::make_shared<core::Image>(
                m_extent, usage, core::memory::MemoryUsage::GPU_ONLY, m_format);

            VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            info.image            = m_historyPairs[i].images[j]->vk();
            info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
            info.format           = m_format;
            info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (vkCreateImageView(device, &info, nullptr, &m_historyPairs[i].views[j]) != VK_SUCCESS)
                throw std::runtime_error("RTTemporalAccumulationRenderGraphPass: failed to create history image view");
        }
    }
}

void RTTemporalAccumulationRenderGraphPass::destroyHistoryImages()
{
    auto context = core::VulkanContext::getContext();
    if (!context)
    {
        m_historyPairs.clear();
        m_pingPong.clear();
        m_historyLayout.clear();
        return;
    }

    const auto device = context->getDevice();
    for (auto &pair : m_historyPairs)
    {
        for (uint32_t j = 0; j < 2u; ++j)
        {
            if (pair.views[j])
            {
                vkDestroyImageView(device, pair.views[j], nullptr);
                pair.views[j] = VK_NULL_HANDLE;
            }
            pair.images[j].reset();
        }
    }
    m_historyPairs.clear();
    m_pingPong.clear();
    m_historyLayout.clear();
}

void RTTemporalAccumulationRenderGraphPass::transitionHistory(VkCommandBuffer cmd,
                                                              VkImage image,
                                                              VkImageLayout from,
                                                              VkImageLayout to)
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

    switch (from)
    {
    case VK_IMAGE_LAYOUT_GENERAL:
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        break;
    default: // UNDEFINED or TRANSFER_DST
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.srcAccessMask = 0;
        break;
    }

    switch (to)
    {
    case VK_IMAGE_LAYOUT_GENERAL:
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        break;
    default:
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        barrier.dstAccessMask = 0;
        break;
    }

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void RTTemporalAccumulationRenderGraphPass::createComputePipeline()
{
    destroyComputePipeline();

    auto context = core::VulkanContext::getContext();
    if (!context)
        return;

    m_computeShader.loadFromFile("./resources/shaders/rt_temporal.comp.spv", core::ShaderStage::COMPUTE);

    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage  = m_computeShader.getInfo();
    ci.layout = m_pipelineLayout;

    if (vkCreateComputePipelines(context->getDevice(), VK_NULL_HANDLE, 1, &ci, nullptr, &m_computePipeline) != VK_SUCCESS)
        throw std::runtime_error("RTTemporalAccumulationRenderGraphPass: failed to create compute pipeline");
}

void RTTemporalAccumulationRenderGraphPass::destroyComputePipeline()
{
    auto context = core::VulkanContext::getContext();
    if (context && m_computePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(context->getDevice(), m_computePipeline, nullptr);
        m_computePipeline = VK_NULL_HANDLE;
    }
    m_computeShader.destroyVk();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
