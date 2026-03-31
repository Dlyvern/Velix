#include "Engine/Render/GraphPasses/RTAORenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include <glm/glm.hpp>
#include <algorithm>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
    struct RTAOpc
    {
        float aoRadius{1.5f};
        float aoSamples{4.0f};
        float enabled{1.0f};
        float padding{0.0f};
    };
}

bool RTAORenderGraphPass::isEnabled() const
{
    const auto &s = RenderQualitySettings::getInstance();
    auto ctx = core::VulkanContext::getContext();
    return s.enableRayTracing && s.enableRTAO && ctx && ctx->hasRayQuerySupport();
}

RTAORenderGraphPass::RTAORenderGraphPass(RGPResourceHandler &depthHandler,
                                         std::vector<RGPResourceHandler> &normalHandlers,
                                         std::vector<RGPResourceHandler> &ssaoHandlers)
    : m_depthHandler(depthHandler), m_normalHandlers(normalHandlers), m_ssaoHandlers(ssaoHandlers)
{
    setDebugName("RTAO render graph pass");
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    outputs.ao.setOwner(this);
}

void RTAORenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    builder.read(m_depthHandler, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this]
                                    { return m_extent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        builder.read(m_normalHandlers[i], RGPTextureUsage::SAMPLED);
        outDesc.setDebugName("__ELIX_RTAO_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT);
    }
    outputs.ao.set(m_outputHandlers);

    for (uint32_t i = 0; i < imageCount; ++i)
        builder.read(m_ssaoHandlers[i], RGPTextureUsage::SAMPLED);

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding normalBinding{};
    normalBinding.binding = 0;
    normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalBinding.descriptorCount = 1;
    normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding depthBinding{};
    depthBinding.binding = 1;
    depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthBinding.descriptorCount = 1;
    depthBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ssaoBinding{};
    ssaoBinding.binding = 2;
    ssaoBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoBinding.descriptorCount = 1;
    ssaoBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{normalBinding, depthBinding, ssaoBinding});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
            *EngineShaderFamilies::cameraDescriptorSetLayout,
            *m_descriptorSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<RTAOpc>::getRange(VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_sampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_depthSampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void RTAORenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputTargets[i] = storage.getTexture(m_outputHandlers[i]);
        auto normalTex = storage.getTexture(m_normalHandlers[i]);
        auto depthTex = storage.getTexture(m_depthHandler);
        auto ssaoTex = storage.getTexture(m_ssaoHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(normalTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .addImage(depthTex->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                                      .addImage(ssaoTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                                      .build(core::VulkanContext::getContext()->getDevice(),
                                             core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                             m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(normalTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(depthTex->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                .addImage(ssaoTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void RTAORenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                 const RenderGraphPassPerFrameData &data,
                                 const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    GraphicsPipelineKey key{};
    key.shader = ShaderId::RTAO;
    key.cull = CullMode::None;
    key.depthTest = false;
    key.depthWrite = false;
    key.depthCompare = VK_COMPARE_OP_LESS;
    key.polygonMode = VK_POLYGON_MODE_FILL;
    key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats = {m_format};
    key.pipelineLayout = m_pipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkDescriptorSet sets[2] = {data.cameraDescriptorSet, m_descriptorSets[renderContext.currentImageIndex]};
    vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 2, sets, 0, nullptr);

    const auto &s = RenderQualitySettings::getInstance();
    auto ctx = core::VulkanContext::getContext();
    RTAOpc pc{};
    pc.aoRadius = std::clamp(s.rtaoRadius, 0.1f, 10.0f);
    pc.aoSamples = static_cast<float>(std::clamp(s.rtaoSamples, 1, 16));
    pc.enabled = (s.enableRayTracing && s.enableRTAO && ctx && ctx->hasRayQuerySupport()) ? 1.0f : 0.0f;

    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<IRenderGraphPass::RenderPassExecution>
RTAORenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_outputTargets[renderContext.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = {.color = {1.0f, 0.0f, 0.0f, 0.0f}};

    exec.colorsRenderingItems = {color};
    exec.useDepth = false;
    exec.colorFormats = {m_format};
    exec.depthFormat = VK_FORMAT_UNDEFINED;
    exec.targets[m_outputHandlers[renderContext.currentImageIndex]] =
        m_outputTargets[renderContext.currentImageIndex];

    return {exec};
}

void RTAORenderGraphPass::setExtent(VkExtent2D extent)
{
    if (m_extent.width == extent.width && m_extent.height == extent.height)
        return;

    m_extent = extent;
    m_viewport = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    m_scissor = {{0, 0}, extent};
    requestRecompilation();
}

void RTAORenderGraphPass::freeResources()
{
    m_outputTargets.clear();
    for (auto &s : m_descriptorSets)
        s = VK_NULL_HANDLE;
    m_descriptorSetsInitialized = false;
    outputs.ao.set(MultiHandle{});
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
