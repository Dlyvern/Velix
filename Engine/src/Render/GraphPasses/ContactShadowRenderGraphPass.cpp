#include "Engine/Render/GraphPasses/ContactShadowRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

bool ContactShadowRenderGraphPass::isEnabled() const
{
    const auto &s = RenderQualitySettings::getInstance();
    return s.enablePostProcessing && s.enableContactShadows;
}

struct ContactShadowPC
{
    float rayLength;
    float strength;
    int   steps;
    float enabled;
};

ContactShadowRenderGraphPass::ContactShadowRenderGraphPass(std::vector<RGPResourceHandler> &hdrInputHandlers,
                                                           std::vector<RGPResourceHandler> &normalHandlers,
                                                           RGPResourceHandler              &depthHandler)
    : m_hdrInputHandlers(hdrInputHandlers),
      m_normalHandlers(normalHandlers),
      m_depthHandler(depthHandler)
{
    setDebugName("Contact Shadow render graph pass");
    m_clearValues[0].color = {0.f, 0.f, 0.f, 1.f};
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    outputs.color.setOwner(this);
}

void ContactShadowRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        builder.read(m_hdrInputHandlers[i], RGPTextureUsage::SAMPLED);
        builder.read(m_normalHandlers[i],   RGPTextureUsage::SAMPLED);
    }
    builder.read(m_depthHandler, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_colorFormat, RGPTextureUsage::COLOR_ATTACHMENT};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this] { return m_extent; });

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_CONTACT_SHADOW_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT);
    }
    outputs.color.set(m_outputHandlers);

    auto device = core::VulkanContext::getContext()->getDevice();

    auto makeBinding = [](uint32_t binding, VkDescriptorType type, VkShaderStageFlags stages) -> VkDescriptorSetLayoutBinding
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding         = binding;
        b.descriptorType  = type;
        b.descriptorCount = 1;
        b.stageFlags      = stages;
        return b;
    };

    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        makeBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT), // HDR color
        makeBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT), // depth
        makeBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT), // normals
    };

    m_textureSetLayout = core::DescriptorSetLayout::createShared(device, bindings);

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
            *EngineShaderFamilies::cameraDescriptorSetLayout,
            *m_textureSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<ContactShadowPC>::getRange(VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_sampler      = core::Sampler::createShared(VK_FILTER_LINEAR,  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_depthSampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void ContactShadowRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputRenderTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    auto depthTarget = storage.getTexture(m_depthHandler);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputRenderTargets[i] = storage.getTexture(m_outputHandlers[i]);

        auto hdrTarget    = storage.getTexture(m_hdrInputHandlers[i]);
        auto normalTarget = storage.getTexture(m_normalHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(hdrTarget->vkImageView(),    m_sampler,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,        0)
                                      .addImage(depthTarget->vkImageView(),  m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                                      .addImage(normalTarget->vkImageView(), m_sampler,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,        2)
                                      .build(core::VulkanContext::getContext()->getDevice(),
                                             core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                             m_textureSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(hdrTarget->vkImageView(),    m_sampler,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,        0)
                .addImage(depthTarget->vkImageView(),  m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                .addImage(normalTarget->vkImageView(), m_sampler,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,        2)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void ContactShadowRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                          const RenderGraphPassPerFrameData &data,
                                          const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    if (data.drawBatches.empty())
        return;

    GraphicsPipelineKey key{};
    key.shader         = ShaderId::ContactShadow;
    key.cull           = CullMode::None;
    key.depthTest      = false;
    key.depthWrite     = false;
    key.depthCompare   = VK_COMPARE_OP_LESS;
    key.polygonMode    = VK_POLYGON_MODE_FILL;
    key.topology       = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats   = {m_colorFormat};
    key.pipelineLayout = m_pipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    std::vector<VkDescriptorSet> sets = {
        data.cameraDescriptorSet,
        m_descriptorSets[renderContext.currentImageIndex]};

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                            0, static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

    const auto &settings = RenderQualitySettings::getInstance();
    ContactShadowPC pc{};
    pc.rayLength = settings.contactShadowLength;
    pc.strength = settings.contactShadowStrength;
    pc.steps   = settings.contactShadowSteps;
    pc.enabled = (settings.enablePostProcessing && settings.enableContactShadows) ? 1.0f : 0.0f;

    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<IRenderGraphPass::RenderPassExecution> ContactShadowRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
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
    exec.colorFormats         = {m_colorFormat};
    exec.depthFormat          = VK_FORMAT_UNDEFINED;

    exec.targets[m_outputHandlers[renderContext.currentImageIndex]] =
        m_outputRenderTargets[renderContext.currentImageIndex];

    return {exec};
}

void ContactShadowRenderGraphPass::setExtent(VkExtent2D extent)
{
    if (m_extent.width == extent.width && m_extent.height == extent.height)
        return;

    m_extent   = extent;
    m_viewport = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    m_scissor  = {{0, 0}, extent};
    requestRecompilation();
}

void ContactShadowRenderGraphPass::freeResources()
{
    m_outputRenderTargets.clear();
    for (auto &s : m_descriptorSets) s = VK_NULL_HANDLE;
    m_descriptorSetsInitialized = false;
    outputs.color.set(MultiHandle{});
}

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END
