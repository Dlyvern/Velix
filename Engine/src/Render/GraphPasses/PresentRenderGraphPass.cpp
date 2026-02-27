#include "Engine/Render/GraphPasses/PresentRenderGraphPass.hpp"

#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

PresentRenderGraphPass::PresentRenderGraphPass(std::vector<RGPResourceHandler> &inputHandlers) : m_colorInputHandlers(inputHandlers)
{
    setDebugName("Present render graph pass");

    m_clearValues[0].color = {0.f, 0.f, 0.f, 1.f};

    auto extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();
    setExtent(extent);
}

void PresentRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_colorFormat = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();

    for (auto &colorImage : m_colorInputHandlers)
        builder.read(colorImage, engine::renderGraph::RGPTextureUsage::SAMPLED);

    engine::renderGraph::RGPTextureDescription ldrDesc{m_colorFormat, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT};
    ldrDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    ldrDesc.setFinalLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    ldrDesc.setIsSwapChainTarget(true);
    ldrDesc.setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        ldrDesc.setDebugName("__ELIX_SCENE_PRESENT_" + std::to_string(i) + "__");
        auto h = builder.createTexture(ldrDesc);
        m_colorTextureHandler.push_back(h);
        builder.write(h, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
    }

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.pImmutableSamplers = nullptr;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(device, std::vector<VkDescriptorSetLayoutBinding>{binding});

    m_pipelineLayout = core::PipelineLayout::createShared(device,
                                                          std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout},
                                                          std::vector<VkPushConstantRange>{});
    m_defaultSampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void PresentRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                                    const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &data.swapChainViewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &data.swapChainScissor);

    GraphicsPipelineKey key{};
    key.shader = ShaderId::Present;
    key.cull = CullMode::None;
    key.depthTest = false;
    key.depthWrite = false;
    key.depthCompare = VK_COMPARE_OP_LESS;
    key.polygonMode = VK_POLYGON_MODE_FILL;
    key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats = {m_colorFormat};
    key.pipelineLayout = m_pipelineLayout;

    auto graphicsPipeline = GraphicsPipelineManager::getOrCreate(key);

    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[renderContext.currentImageIndex], 0, nullptr);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<PresentRenderGraphPass::RenderPassExecution> PresentRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color0{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color0.imageView = m_colorRenderTargets[renderContext.currentImageIndex]->vkImageView();
    color0.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color0.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color0.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color0.clearValue = m_clearValues[0];

    exec.colorsRenderingItems = {color0};
    exec.useDepth = false;
    exec.colorFormats = {m_colorFormat};
    exec.depthFormat = VK_FORMAT_UNDEFINED;

    exec.targets[m_colorTextureHandler[renderContext.currentImageIndex]] =
        m_colorRenderTargets[renderContext.currentImageIndex];

    return {exec};
}

void PresentRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    m_scissor = {{0, 0}, extent};
    requestRecompilation();
}

void PresentRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    m_colorRenderTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_colorRenderTargets[i] = storage.getTexture(m_colorTextureHandler[i]);

        auto texture = storage.getTexture(m_colorInputHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(texture->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .build(core::VulkanContext::getContext()->getDevice(), core::VulkanContext::getContext()->getPersistentDescriptorPool(), m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(texture->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END
