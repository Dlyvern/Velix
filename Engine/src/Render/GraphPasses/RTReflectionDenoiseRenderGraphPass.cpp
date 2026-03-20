#include "Engine/Render/GraphPasses/RTReflectionDenoiseRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Shaders/PushConstant.hpp"

#include <algorithm>
#include <glm/gtc/matrix_inverse.hpp>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

RTReflectionDenoiseRenderGraphPass::RTReflectionDenoiseRenderGraphPass(
    std::vector<RGPResourceHandler> &reflectionHandlers,
    std::vector<RGPResourceHandler> &normalHandlers,
    RGPResourceHandler &depthHandler)
    : m_reflectionHandlers(reflectionHandlers),
      m_normalHandlers(normalHandlers),
      m_depthHandler(depthHandler)
{
    setDebugName("RT Reflection denoise render graph pass");
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    outputs.color.setOwner(this);
}

void RTReflectionDenoiseRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        builder.read(m_reflectionHandlers[i], RGPTextureUsage::SAMPLED);
        builder.read(m_normalHandlers[i], RGPTextureUsage::SAMPLED);
    }
    builder.read(m_depthHandler, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outputDescription{m_reflectionFormat, RGPTextureUsage::COLOR_ATTACHMENT_STORAGE};
    outputDescription.setInitialLayout(VK_IMAGE_LAYOUT_GENERAL);
    outputDescription.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outputDescription.setCustomExtentFunction([this] { return m_extent; });

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outputDescription.setDebugName("__ELIX_RT_REFLECTION_DENOISE_" + std::to_string(i) + "__");
        auto handler = builder.createTexture(outputDescription);
        m_outputHandlers.push_back(handler);
        builder.write(handler, RGPTextureUsage::COLOR_ATTACHMENT_STORAGE);
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
            PushConstant<RTReflectionDenoisePC>::getRange(VK_SHADER_STAGE_COMPUTE_BIT)});

    m_reflectionSampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_normalSampler     = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_depthSampler      = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    createComputePipeline();
}

void RTReflectionDenoiseRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputRenderTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    const auto *depthTarget = storage.getTexture(m_depthHandler);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        const auto *reflectionTarget = storage.getTexture(m_reflectionHandlers[i]);
        const auto *normalTarget     = storage.getTexture(m_normalHandlers[i]);
        const auto *outputTarget     = storage.getTexture(m_outputHandlers[i]);

        m_outputRenderTargets[i] = outputTarget;

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(reflectionTarget->vkImageView(), m_reflectionSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,        0)
                                      .addImage(normalTarget->vkImageView(),     m_normalSampler,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,        1)
                                      .addImage(depthTarget->vkImageView(),      m_depthSampler,      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 2)
                                      .addStorageImage(outputTarget->vkImageView(), VK_IMAGE_LAYOUT_GENERAL, 3)
                                      .build(core::VulkanContext::getContext()->getDevice(),
                                             core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                             m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(reflectionTarget->vkImageView(), m_reflectionSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,        0)
                .addImage(normalTarget->vkImageView(),     m_normalSampler,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,        1)
                .addImage(depthTarget->vkImageView(),      m_depthSampler,      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 2)
                .addStorageImage(outputTarget->vkImageView(), VK_IMAGE_LAYOUT_GENERAL, 3)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void RTReflectionDenoiseRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                                const RenderGraphPassPerFrameData &data,
                                                const RenderGraphPassContext &renderContext)
{
    const auto *outputTarget = m_outputRenderTargets[renderContext.currentImageIndex];
    if (!outputTarget || m_extent.width == 0u || m_extent.height == 0u)
        return;

    if (data.drawBatches.empty())
    {
        VkClearColorValue clearColor{};
        VkImageSubresourceRange range{};
        range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel   = 0;
        range.levelCount     = 1;
        range.baseArrayLayer = 0;
        range.layerCount     = 1;
        vkCmdClearColorImage(commandBuffer->vk(), outputTarget->getImage()->vk(),
                             VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);
        return;
    }

    const auto &settings = RenderQualitySettings::getInstance();
    const bool denoiseEnabled =
        settings.enableRayTracing &&
        settings.enableRTReflections &&
        settings.rayTracingMode == RenderQualitySettings::RayTracingMode::RayQuery;
    RTReflectionDenoisePC pushConstants{};
    pushConstants.invProjection = glm::inverse(data.projection);
    pushConstants.params0 = glm::vec4(
        1.0f / static_cast<float>(m_extent.width),
        1.0f / static_cast<float>(m_extent.height),
        denoiseEnabled ? 1.0f : 0.0f,
        0.1f);
    pushConstants.params1 = glm::vec4(0.5f, 0.0f, 0.0f, 0.0f);

    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(commandBuffer->vk(),
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_pipelineLayout,
                            0, 1,
                            &m_descriptorSets[renderContext.currentImageIndex],
                            0, nullptr);
    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);

    const uint32_t groupCountX = (m_extent.width  + 7u) / 8u;
    const uint32_t groupCountY = (m_extent.height + 7u) / 8u;
    vkCmdDispatch(commandBuffer->vk(), groupCountX, groupCountY, 1u);
}

std::vector<IRenderGraphPass::RenderPassExecution>
RTReflectionDenoiseRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution execution{};
    execution.mode        = IRenderGraphPass::ExecutionMode::Direct;
    execution.useDepth    = false;
    execution.depthFormat = VK_FORMAT_UNDEFINED;
    execution.renderArea.offset = {0, 0};
    execution.renderArea.extent = m_extent;
    execution.targets[m_outputHandlers[renderContext.currentImageIndex]] = m_outputRenderTargets[renderContext.currentImageIndex];
    return {execution};
}

void RTReflectionDenoiseRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    requestRecompilation();
}

void RTReflectionDenoiseRenderGraphPass::cleanup()
{
    destroyComputePipeline();
}

void RTReflectionDenoiseRenderGraphPass::createComputePipeline()
{
    destroyComputePipeline();

    auto context = core::VulkanContext::getContext();
    if (!context)
        return;

    m_computeShader.loadFromFile("./resources/shaders/rt_reflection_denoise.comp.spv", core::ShaderStage::COMPUTE);

    VkComputePipelineCreateInfo pipelineCreateInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineCreateInfo.stage  = m_computeShader.getInfo();
    pipelineCreateInfo.layout = m_pipelineLayout;

    if (vkCreateComputePipelines(context->getDevice(), VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_computePipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create RT reflection denoise compute pipeline");
}

void RTReflectionDenoiseRenderGraphPass::destroyComputePipeline()
{
    auto context = core::VulkanContext::getContext();
    if (context && m_computePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(context->getDevice(), m_computePipeline, nullptr);
        m_computePipeline = VK_NULL_HANDLE;
    }
    m_computeShader.destroyVk();
}

void RTReflectionDenoiseRenderGraphPass::freeResources()
{
    m_outputRenderTargets.clear();
    for (auto &s : m_descriptorSets) s = VK_NULL_HANDLE;
    m_descriptorSetsInitialized = false;
    outputs.color.set(MultiHandle{});
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
