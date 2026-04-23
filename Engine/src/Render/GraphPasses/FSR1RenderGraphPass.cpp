#include "Engine/Render/GraphPasses/FSR1RenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

#include <glm/glm.hpp>

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/PushConstant.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
constexpr uint32_t EXEC_EASU = 0u;
constexpr uint32_t EXEC_RCAS = 1u;
}

bool FSR1RenderGraphPass::isEnabled() const
{
    const auto &s = RenderQualitySettings::getInstance();
    return s.enablePostProcessing &&
           s.upscalerMode == RenderQualitySettings::UpscalerMode::FSR1;
}

struct FSR1PC
{
    glm::vec4 a;
    glm::vec4 b;
};

FSR1RenderGraphPass::FSR1RenderGraphPass(std::vector<RGPResourceHandler> &inputHandlers)
    : m_inputHandlers(inputHandlers)
{
    setDebugName("FSR1 render graph pass");
    m_clearValues[0].color = {0.f, 0.f, 0.f, 1.f};
    const auto swapExtent = core::VulkanContext::getContext()->getSwapchain()->getExtent();
    setExtents(swapExtent, swapExtent);
    outputs.color.setOwner(this);
}

void FSR1RenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_format = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();

    for (auto &h : m_inputHandlers)
        builder.read(h, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this] { return m_outputExtent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_FSR1_EASU_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_intermediateHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT);
        builder.read(h, RGPTextureUsage::SAMPLED);
    }
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_FSR1_RCAS_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_finalHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT);
    }
    outputs.color.set(m_finalHandlers);

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{binding});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<FSR1PC>::getRange(VK_SHADER_STAGE_FRAGMENT_BIT)});



    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR,
                                            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                            VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void FSR1RenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_intermediateRenderTargets.resize(imageCount);
    m_finalRenderTargets.resize(imageCount);
    m_easuDescriptorSets.resize(imageCount);
    m_rcasDescriptorSets.resize(imageCount);

    auto device = core::VulkanContext::getContext()->getDevice();
    auto pool   = core::VulkanContext::getContext()->getPersistentDescriptorPool();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_intermediateRenderTargets[i] = storage.getTexture(m_intermediateHandlers[i]);
        m_finalRenderTargets[i]        = storage.getTexture(m_finalHandlers[i]);

        auto inputTarget        = storage.getTexture(m_inputHandlers[i]);
        auto intermediateTarget = m_intermediateRenderTargets[i];

        if (!m_descriptorSetsInitialized)
        {
            m_easuDescriptorSets[i] = DescriptorSetBuilder::begin()
                                          .addImage(inputTarget->vkImageView(), m_sampler,
                                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                          .build(device, pool, m_descriptorSetLayout);
            m_rcasDescriptorSets[i] = DescriptorSetBuilder::begin()
                                          .addImage(intermediateTarget->vkImageView(), m_sampler,
                                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                          .build(device, pool, m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(inputTarget->vkImageView(), m_sampler,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .update(device, m_easuDescriptorSets[i]);
            DescriptorSetBuilder::begin()
                .addImage(intermediateTarget->vkImageView(), m_sampler,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .update(device, m_rcasDescriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void FSR1RenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                 const RenderGraphPassPerFrameData & ,
                                 const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    GraphicsPipelineKey key{};
    key.cull           = CullMode::None;
    key.depthTest      = false;
    key.depthWrite     = false;
    key.depthCompare   = VK_COMPARE_OP_LESS;
    key.polygonMode    = VK_POLYGON_MODE_FILL;
    key.topology       = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats   = {m_format};
    key.pipelineLayout = m_pipelineLayout;

    const auto &settings = RenderQualitySettings::getInstance();
    FSR1PC pc{};

    if (renderContext.executionIndex == EXEC_EASU)
    {
        key.shader = ShaderId::FSR1EASU;
        pc.a = glm::vec4(
            static_cast<float>(m_inputExtent.width),
            static_cast<float>(m_inputExtent.height),
            1.0f / std::max(1u, m_inputExtent.width),
            1.0f / std::max(1u, m_inputExtent.height));
        pc.b = glm::vec4(
            static_cast<float>(m_outputExtent.width),
            static_cast<float>(m_outputExtent.height),
            1.0f / std::max(1u, m_outputExtent.width),
            1.0f / std::max(1u, m_outputExtent.height));

        auto pipeline = GraphicsPipelineManager::getOrCreate(key);
        vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                0, 1, &m_easuDescriptorSets[renderContext.currentImageIndex], 0, nullptr);
    }
    else
    {
        key.shader = ShaderId::FSR1RCAS;
        pc.a = glm::vec4(
            static_cast<float>(m_outputExtent.width),
            static_cast<float>(m_outputExtent.height),
            1.0f / std::max(1u, m_outputExtent.width),
            1.0f / std::max(1u, m_outputExtent.height));
        pc.b = glm::vec4(std::clamp(settings.fsrSharpness, 0.0f, 2.0f), 0.0f, 0.0f, 0.0f);

        auto pipeline = GraphicsPipelineManager::getOrCreate(key);
        vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                0, 1, &m_rcasDescriptorSets[renderContext.currentImageIndex], 0, nullptr);
    }

    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<IRenderGraphPass::RenderPassExecution>
FSR1RenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    std::vector<RenderPassExecution> out;
    out.reserve(2);


    VkRect2D renderArea{};
    renderArea.offset = {0, 0};
    renderArea.extent = m_outputExtent;


    {
        RenderPassExecution exec{};
        exec.renderArea = renderArea;

        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView   = m_intermediateRenderTargets[renderContext.currentImageIndex]->vkImageView();
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue  = m_clearValues[0];

        exec.colorsRenderingItems = {color};
        exec.useDepth             = false;
        exec.colorFormats         = {m_format};
        exec.depthFormat          = VK_FORMAT_UNDEFINED;

        exec.targets[m_intermediateHandlers[renderContext.currentImageIndex]] =
            m_intermediateRenderTargets[renderContext.currentImageIndex];

        out.push_back(std::move(exec));
    }


    {
        RenderPassExecution exec{};
        exec.renderArea = renderArea;

        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView   = m_finalRenderTargets[renderContext.currentImageIndex]->vkImageView();
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue  = m_clearValues[0];

        exec.colorsRenderingItems = {color};
        exec.useDepth             = false;
        exec.colorFormats         = {m_format};
        exec.depthFormat          = VK_FORMAT_UNDEFINED;

        exec.targets[m_finalHandlers[renderContext.currentImageIndex]] =
            m_finalRenderTargets[renderContext.currentImageIndex];

        out.push_back(std::move(exec));
    }

    return out;
}

void FSR1RenderGraphPass::setExtents(VkExtent2D inputExtent, VkExtent2D outputExtent)
{
    if (m_inputExtent.width == inputExtent.width && m_inputExtent.height == inputExtent.height &&
        m_outputExtent.width == outputExtent.width && m_outputExtent.height == outputExtent.height)
        return;

    m_inputExtent  = inputExtent;
    m_outputExtent = outputExtent;
    m_viewport = {0.0f, 0.0f, (float)outputExtent.width, (float)outputExtent.height, 0.0f, 1.0f};
    m_scissor  = {{0, 0}, outputExtent};
    requestRecompilation();
}

void FSR1RenderGraphPass::freeResources()
{
    m_intermediateRenderTargets.clear();
    m_finalRenderTargets.clear();
    for (auto &s : m_easuDescriptorSets) s = VK_NULL_HANDLE;
    for (auto &s : m_rcasDescriptorSets) s = VK_NULL_HANDLE;
    m_descriptorSetsInitialized = false;
    outputs.color.set(MultiHandle{});
}

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END
