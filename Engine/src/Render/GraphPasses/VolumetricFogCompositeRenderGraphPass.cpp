#include "Engine/Render/GraphPasses/VolumetricFogCompositeRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

VolumetricFogCompositeRenderGraphPass::VolumetricFogCompositeRenderGraphPass(
    std::vector<RGPResourceHandler> &sceneColorHandlers,
    std::vector<RGPResourceHandler> &fogHandlers)
    : m_sceneColorHandlers(sceneColorHandlers), m_fogHandlers(fogHandlers)
{
    setDebugName("Volumetric fog composite render graph pass");
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    outputs.color.setOwner(this);
}

void VolumetricFogCompositeRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_outputHandlers.clear();

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this] { return m_extent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        builder.read(m_sceneColorHandlers[i], RGPTextureUsage::SAMPLED);
        builder.read(m_fogHandlers[i], RGPTextureUsage::SAMPLED);

        outDesc.setDebugName("__ELIX_VOLUMETRIC_FOG_COMPOSITE_" + std::to_string(i) + "__");
        auto handler = builder.createTexture(outDesc);
        m_outputHandlers.push_back(handler);
        builder.write(handler, RGPTextureUsage::COLOR_ATTACHMENT);
    }
    outputs.color.set(m_outputHandlers);

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding sceneColorBinding{};
    sceneColorBinding.binding = 0;
    sceneColorBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sceneColorBinding.descriptorCount = 1;
    sceneColorBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding fogBinding{};
    fogBinding.binding = 1;
    fogBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    fogBinding.descriptorCount = 1;
    fogBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(
        device,
        std::vector<VkDescriptorSetLayoutBinding>{sceneColorBinding, fogBinding});
    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout});

    m_sampler = core::Sampler::createShared(
        VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void VolumetricFogCompositeRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        const auto *sceneColorTexture = storage.getTexture(m_sceneColorHandlers[i]);
        const auto *fogTexture = storage.getTexture(m_fogHandlers[i]);
        m_outputTargets[i] = storage.getTexture(m_outputHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(sceneColorTexture->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .addImage(fogTexture->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                                      .build(core::VulkanContext::getContext()->getDevice(),
                                             core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                             m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(sceneColorTexture->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(fogTexture->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void VolumetricFogCompositeRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                                   const RenderGraphPassPerFrameData &,
                                                   const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    GraphicsPipelineKey key{};
    key.shader = ShaderId::VolumetricFogComposite;
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
    vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &m_descriptorSets[renderContext.currentImageIndex], 0, nullptr);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<IRenderGraphPass::RenderPassExecution>
VolumetricFogCompositeRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_outputTargets[renderContext.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = m_clearValues[0];

    exec.colorsRenderingItems = {color};
    exec.useDepth = false;
    exec.colorFormats = {m_format};
    exec.depthFormat = VK_FORMAT_UNDEFINED;
    exec.targets[m_outputHandlers[renderContext.currentImageIndex]] = m_outputTargets[renderContext.currentImageIndex];
    return {exec};
}

void VolumetricFogCompositeRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = {0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    m_scissor = {{0, 0}, m_extent};
    requestRecompilation();
}

void VolumetricFogCompositeRenderGraphPass::freeResources()
{
    m_outputHandlers.clear();
    m_outputTargets.clear();
    for (auto &descriptorSet : m_descriptorSets)
        descriptorSet = VK_NULL_HANDLE;
    m_descriptorSetsInitialized = false;
    outputs.color.set(MultiHandle{});
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
