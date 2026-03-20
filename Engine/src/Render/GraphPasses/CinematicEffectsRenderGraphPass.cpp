#include "Engine/Render/GraphPasses/CinematicEffectsRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/PushConstant.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

bool CinematicEffectsRenderGraphPass::isEnabled() const
{
    const auto &s = RenderQualitySettings::getInstance();
    return s.enablePostProcessing && (s.enableVignette || s.enableFilmGrain || s.enableChromaticAberration);
}

struct CinematicPC
{
    float vignetteStrength;
    float grainStrength;
    float aberrationStrength;
    float time;
    float vignetteEnabled;
    float grainEnabled;
    float aberrationEnabled;
    float _pad;
};

CinematicEffectsRenderGraphPass::CinematicEffectsRenderGraphPass(std::vector<RGPResourceHandler> &inputHandlers)
    : m_inputHandlers(inputHandlers)
{
    setDebugName("Cinematic Effects render graph pass");
    outputs.color.setOwner(this);
    m_clearValues[0].color = {0.f, 0.f, 0.f, 1.f};
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void CinematicEffectsRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_format = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();

    for (auto &h : m_inputHandlers)
        builder.read(h, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this] { return m_extent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_CINEMATIC_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT);
    }
    outputs.color.set(m_outputHandlers);

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(device, std::vector<VkDescriptorSetLayoutBinding>{binding});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<CinematicPC>::getRange(VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void CinematicEffectsRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputRenderTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputRenderTargets[i] = storage.getTexture(m_outputHandlers[i]);
        auto inputTarget         = storage.getTexture(m_inputHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(inputTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .build(core::VulkanContext::getContext()->getDevice(),
                                             core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                             m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(inputTarget->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void CinematicEffectsRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                             const RenderGraphPassPerFrameData & /*data*/,
                                             const RenderGraphPassContext &renderContext)
{
    m_time += 1.0f / 60.0f;

    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    GraphicsPipelineKey key{};
    key.shader         = ShaderId::CinematicEffects;
    key.cull           = CullMode::None;
    key.depthTest      = false;
    key.depthWrite     = false;
    key.depthCompare   = VK_COMPARE_OP_LESS;
    key.polygonMode    = VK_POLYGON_MODE_FILL;
    key.topology       = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats   = {m_format};
    key.pipelineLayout = m_pipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                            0, 1, &m_descriptorSets[renderContext.currentImageIndex], 0, nullptr);

    const auto &settings = RenderQualitySettings::getInstance();
    const bool  pp       = settings.enablePostProcessing;
    CinematicPC pc{};
    pc.vignetteStrength    = settings.vignetteStrength;
    pc.grainStrength       = settings.filmGrainStrength;
    pc.aberrationStrength  = settings.chromaticAberrationStrength;
    pc.time                = m_time;
    pc.vignetteEnabled     = (pp && settings.enableVignette)            ? 1.0f : 0.0f;
    pc.grainEnabled        = (pp && settings.enableFilmGrain)           ? 1.0f : 0.0f;
    pc.aberrationEnabled   = (pp && settings.enableChromaticAberration) ? 1.0f : 0.0f;

    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<IRenderGraphPass::RenderPassExecution> CinematicEffectsRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
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

void CinematicEffectsRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent   = extent;
    m_viewport = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    m_scissor  = {{0, 0}, extent};
    requestRecompilation();
}

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END
