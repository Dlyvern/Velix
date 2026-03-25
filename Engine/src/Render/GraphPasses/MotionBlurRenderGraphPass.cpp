#include "Engine/Render/GraphPasses/MotionBlurRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/PushConstant.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
    struct MotionBlurPC
    {
        glm::mat4 invViewProj;
        glm::mat4 prevViewProj;
        glm::vec2 texelSize;
        float     intensity;
        float     numSamplesF;
    };
}

bool MotionBlurRenderGraphPass::isEnabled() const
{
    const auto &s = RenderQualitySettings::getInstance();
    return s.enablePostProcessing && s.enableMotionBlur;
}

MotionBlurRenderGraphPass::MotionBlurRenderGraphPass(std::vector<RGPResourceHandler> &inputHandlers,
                                                     RGPResourceHandler              &depthHandler)
    : m_inputHandlers(inputHandlers), m_depthHandler(depthHandler)
{
    setDebugName("Motion Blur render graph pass");
    m_clearValues[0].color = {0.f, 0.f, 0.f, 1.f};
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    outputs.color.setOwner(this);
}

void MotionBlurRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_format = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();

    builder.read(m_depthHandler, RGPTextureUsage::SAMPLED);

    for (auto &h : m_inputHandlers)
        builder.read(h, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this] { return m_extent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_MOTION_BLUR_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT);
    }
    outputs.color.set(m_outputHandlers);

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding colorBinding{};
    colorBinding.binding        = 0;
    colorBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    colorBinding.descriptorCount = 1;
    colorBinding.stageFlags     = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding depthBinding{};
    depthBinding.binding        = 1;
    depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthBinding.descriptorCount = 1;
    depthBinding.stageFlags     = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{colorBinding, depthBinding});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<MotionBlurPC>::getRange(VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR,  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_depthSampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void MotionBlurRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputRenderTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    auto depthTex = storage.getTexture(m_depthHandler);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputRenderTargets[i] = storage.getTexture(m_outputHandlers[i]);
        auto colorTex = storage.getTexture(m_inputHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                .addImage(colorTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(depthTex->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                .build(core::VulkanContext::getContext()->getDevice(),
                       core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                       m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(colorTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(depthTex->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void MotionBlurRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                       const RenderGraphPassPerFrameData &data,
                                       const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    GraphicsPipelineKey key{};
    key.shader       = ShaderId::MotionBlur;
    key.cull         = CullMode::None;
    key.depthTest    = false;
    key.depthWrite   = false;
    key.depthCompare = VK_COMPARE_OP_LESS;
    key.polygonMode  = VK_POLYGON_MODE_FILL;
    key.topology     = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats = {m_format};
    key.pipelineLayout = m_pipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                            0, 1, &m_descriptorSets[renderContext.currentImageIndex], 0, nullptr);

    const auto &s = RenderQualitySettings::getInstance();

    // First frame: no previous matrices, use current as prev (no blur)
    if (!m_hasPrevFrame)
    {
        m_prevView       = data.view;
        m_prevProjection = data.projection;
        m_hasPrevFrame   = true;
    }

    MotionBlurPC pc{};
    pc.invViewProj  = glm::inverse(data.projection * data.view);
    pc.prevViewProj = m_prevProjection * m_prevView;
    pc.texelSize    = {1.0f / static_cast<float>(m_extent.width), 1.0f / static_cast<float>(m_extent.height)};
    pc.intensity    = s.motionBlurIntensity;
    pc.numSamplesF  = static_cast<float>(glm::clamp(s.motionBlurSamples, 2, 32));

    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);

    // Store current matrices for next frame
    m_prevView       = data.view;
    m_prevProjection = data.projection;
}

std::vector<IRenderGraphPass::RenderPassExecution>
MotionBlurRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView   = m_outputRenderTargets[renderContext.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue  = m_clearValues[0];

    exec.colorsRenderingItems = {color};
    exec.useDepth             = false;
    exec.colorFormats         = {m_format};
    exec.depthFormat          = VK_FORMAT_UNDEFINED;

    exec.targets[m_outputHandlers[renderContext.currentImageIndex]] =
        m_outputRenderTargets[renderContext.currentImageIndex];

    return {exec};
}

void MotionBlurRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent   = extent;
    m_viewport = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    m_scissor  = {{0, 0}, extent};
    requestRecompilation();
}

void MotionBlurRenderGraphPass::freeResources()
{
    m_outputRenderTargets.clear();
    m_outputHandlers.clear();
    for (auto &s : m_descriptorSets) s = VK_NULL_HANDLE;
    m_descriptorSetsInitialized = false;
    outputs.color.set(MultiHandle{});
}

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END
