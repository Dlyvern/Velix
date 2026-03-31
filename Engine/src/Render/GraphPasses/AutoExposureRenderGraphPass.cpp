#include "Engine/Render/GraphPasses/AutoExposureRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Shaders/PushConstant.hpp"

#include <cstring>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
constexpr uint32_t kHistogramBins  = 256u;
constexpr uint32_t kHistogramBytes = kHistogramBins * sizeof(uint32_t); // 1 KiB

// Luminance range (log2 EV) over which bins are distributed.
// EV -8 → very dark scene, EV +8 → very bright scene.
constexpr float kMinLogLum    = -8.0f;
constexpr float kMaxLogLum    =  8.0f;
constexpr float kLogLumRange  = kMaxLogLum - kMinLogLum;
} // namespace

AutoExposureRenderGraphPass::AutoExposureRenderGraphPass(std::vector<RGPResourceHandler> &hdrHandlers)
    : m_hdrHandlers(hdrHandlers)
{
    setDebugName("Auto Exposure");
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void AutoExposureRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    for (uint32_t i = 0; i < imageCount; ++i)
        builder.read(m_hdrHandlers[i], RGPTextureUsage::SAMPLED);

    auto device = core::VulkanContext::getContext()->getDevice();

    auto makeBinding = [](uint32_t binding, VkDescriptorType type) -> VkDescriptorSetLayoutBinding
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding        = binding;
        b.descriptorType = type;
        b.descriptorCount = 1;
        b.stageFlags     = VK_SHADER_STAGE_COMPUTE_BIT;
        return b;
    };

    // Histogram pass: binding 0 = HDR sampler, binding 1 = histogram SSBO (write)
    m_histDescriptorSetLayout = core::DescriptorSetLayout::createShared(
        device,
        std::vector<VkDescriptorSetLayoutBinding>{
            makeBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            makeBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)});

    // Average pass: binding 0 = histogram SSBO (read), binding 1 = exposure SSBO (r/w)
    m_avgDescriptorSetLayout = core::DescriptorSetLayout::createShared(
        device,
        std::vector<VkDescriptorSetLayoutBinding>{
            makeBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            makeBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)});

    m_histPipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_histDescriptorSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<HistogramPC>::getRange(VK_SHADER_STAGE_COMPUTE_BIT)});

    m_avgPipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_avgDescriptorSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<AveragePC>::getRange(VK_SHADER_STAGE_COMPUTE_BIT)});

    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                            VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    // Pre-allocate persistent GPU buffers.
    m_histogramBuffers.resize(imageCount);
    m_exposureBuffers.resize(imageCount);
    m_exposureMapped.resize(imageCount, nullptr);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_histogramBuffers[i] = core::Buffer::createShared(
            kHistogramBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            core::memory::MemoryUsage::GPU_ONLY);

        m_exposureBuffers[i] = core::Buffer::createShared(
            sizeof(float),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            core::memory::MemoryUsage::GPU_TO_CPU);

        // Initialise exposure to 1.0 so the first frame doesn't flash.
        m_exposureBuffers[i]->map(m_exposureMapped[i]);
        float init = 1.0f;
        std::memcpy(m_exposureMapped[i], &init, sizeof(float));
        // Keep persistently mapped — GPU_TO_CPU memory allows this.
    }

    createPipelines();
}

void AutoExposureRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    const auto     device     = core::VulkanContext::getContext()->getDevice();
    const auto     pool       = core::VulkanContext::getContext()->getPersistentDescriptorPool();

    m_histDescriptorSets.resize(imageCount);
    m_avgDescriptorSets.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        const auto *hdrTarget = storage.getTexture(m_hdrHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_histDescriptorSets[i] = DescriptorSetBuilder::begin()
                .addImage(hdrTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addBuffer(m_histogramBuffers[i], kHistogramBytes, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .build(device, pool, m_histDescriptorSetLayout);

            m_avgDescriptorSets[i] = DescriptorSetBuilder::begin()
                .addBuffer(m_histogramBuffers[i], kHistogramBytes, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .addBuffer(m_exposureBuffers[i],  sizeof(float),   1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .build(device, pool, m_avgDescriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(hdrTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addBuffer(m_histogramBuffers[i], kHistogramBytes, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .update(device, m_histDescriptorSets[i]);

            DescriptorSetBuilder::begin()
                .addBuffer(m_histogramBuffers[i], kHistogramBytes, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .addBuffer(m_exposureBuffers[i],  sizeof(float),   1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .update(device, m_avgDescriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void AutoExposureRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                         const RenderGraphPassPerFrameData &data,
                                         const RenderGraphPassContext      &renderContext)
{
    if (m_histPipeline == VK_NULL_HANDLE || m_avgPipeline == VK_NULL_HANDLE)
        return;
    if (m_extent.width == 0u || m_extent.height == 0u)
        return;

    const uint32_t idx = renderContext.currentImageIndex;
    VkCommandBuffer cmd = commandBuffer->vk();

    const auto &settings = RenderQualitySettings::getInstance();

    // ── Pass 1: build luminance histogram ──────────────────────────────────
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_histPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_histPipelineLayout, 0, 1, &m_histDescriptorSets[idx], 0, nullptr);

    HistogramPC hpc{};
    hpc.minLogLum      = kMinLogLum;
    hpc.rcpLogLumRange = 1.0f / kLogLumRange;
    hpc.width          = m_extent.width;
    hpc.height         = m_extent.height;
    vkCmdPushConstants(cmd, m_histPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(hpc), &hpc);

    const uint32_t gx = (m_extent.width  + 15u) / 16u;
    const uint32_t gy = (m_extent.height + 15u) / 16u;
    vkCmdDispatch(cmd, gx, gy, 1u);

    // Barrier: histogram write → average read.
    VkBufferMemoryBarrier2 histBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    histBarrier.srcStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    histBarrier.srcAccessMask       = VK_ACCESS_2_SHADER_WRITE_BIT;
    histBarrier.dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    histBarrier.dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT;
    histBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    histBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    histBarrier.buffer              = m_histogramBuffers[idx]->vk();
    histBarrier.offset              = 0;
    histBarrier.size                = VK_WHOLE_SIZE;

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers    = &histBarrier;
    vkCmdPipelineBarrier2(cmd, &dep);

    // ── Pass 2: reduce histogram → adapted exposure ────────────────────────
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_avgPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_avgPipelineLayout, 0, 1, &m_avgDescriptorSets[idx], 0, nullptr);

    AveragePC apc{};
    apc.minLogLum   = kMinLogLum;
    apc.logLumRange = kLogLumRange;
    apc.deltaTime   = data.deltaTime;
    apc.speedUp     = settings.autoExposureSpeedUp;
    apc.speedDown   = settings.autoExposureSpeedDown;
    apc.lowPercent  = settings.autoExposureLowPercent;
    apc.highPercent = settings.autoExposureHighPercent;
    apc.pad         = 0u;
    vkCmdPushConstants(cmd, m_avgPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(apc), &apc);

    // Single workgroup of 256 threads covers all 256 bins.
    vkCmdDispatch(cmd, 1u, 1u, 1u);

    // Barrier: exposure write (GPU) → host read (CPU next frame).
    VkBufferMemoryBarrier2 expBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    expBarrier.srcStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    expBarrier.srcAccessMask       = VK_ACCESS_2_SHADER_WRITE_BIT;
    expBarrier.dstStageMask        = VK_PIPELINE_STAGE_2_HOST_BIT;
    expBarrier.dstAccessMask       = VK_ACCESS_2_HOST_READ_BIT;
    expBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    expBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    expBarrier.buffer              = m_exposureBuffers[idx]->vk();
    expBarrier.offset              = 0;
    expBarrier.size                = VK_WHOLE_SIZE;

    VkDependencyInfo dep2{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep2.bufferMemoryBarrierCount = 1;
    dep2.pBufferMemoryBarriers    = &expBarrier;
    vkCmdPipelineBarrier2(cmd, &dep2);
}

std::vector<IRenderGraphPass::RenderPassExecution>
AutoExposureRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.mode             = IRenderGraphPass::ExecutionMode::Direct;
    exec.useDepth         = false;
    exec.depthFormat      = VK_FORMAT_UNDEFINED;
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;
    return {exec};
}

void AutoExposureRenderGraphPass::setExtent(VkExtent2D extent)
{
    if (m_extent.width == extent.width && m_extent.height == extent.height)
        return;
    m_extent = extent;
    requestRecompilation();
}

float AutoExposureRenderGraphPass::getAdaptedExposure(uint32_t imageIndex) const
{
    if (imageIndex >= m_exposureMapped.size() || !m_exposureMapped[imageIndex])
        return 1.0f;
    float e = 1.0f;
    std::memcpy(&e, m_exposureMapped[imageIndex], sizeof(float));
    return (e > 0.0f) ? e : 1.0f;
}

void AutoExposureRenderGraphPass::cleanup()
{
    destroyPipelines();

    // Unmap and release exposure buffers.
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_exposureBuffers.size()); ++i)
    {
        if (m_exposureMapped[i] && m_exposureBuffers[i])
        {
            m_exposureBuffers[i]->unmap();
            m_exposureMapped[i] = nullptr;
        }
    }
    m_histogramBuffers.clear();
    m_exposureBuffers.clear();
    m_exposureMapped.clear();
}

void AutoExposureRenderGraphPass::freeResources()
{
    m_histDescriptorSets.clear();
    m_avgDescriptorSets.clear();
    m_descriptorSetsInitialized = false;
}

void AutoExposureRenderGraphPass::createPipelines()
{
    destroyPipelines();

    auto context = core::VulkanContext::getContext();
    if (!context)
        return;

    auto createCompute = [&](core::ShaderHandler &shader, const char *path,
                              VkPipelineLayout layout, VkPipeline &pipeline)
    {
        shader.loadFromFile(path, core::ShaderStage::COMPUTE);
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage  = shader.getInfo();
        ci.layout = layout;
        if (vkCreateComputePipelines(context->getDevice(), VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline) != VK_SUCCESS)
            throw std::runtime_error("Failed to create auto exposure compute pipeline");
    };

    createCompute(m_histShader, "./resources/shaders/auto_exposure_histogram.comp.spv",
                  m_histPipelineLayout, m_histPipeline);
    createCompute(m_avgShader,  "./resources/shaders/auto_exposure_average.comp.spv",
                  m_avgPipelineLayout,  m_avgPipeline);
}

void AutoExposureRenderGraphPass::destroyPipelines()
{
    auto context = core::VulkanContext::getContext();
    if (!context)
        return;
    const auto dev = context->getDevice();
    if (m_histPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(dev, m_histPipeline, nullptr);
        m_histPipeline = VK_NULL_HANDLE;
    }
    if (m_avgPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(dev, m_avgPipeline, nullptr);
        m_avgPipeline = VK_NULL_HANDLE;
    }
    m_histShader.destroyVk();
    m_avgShader.destroyVk();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
