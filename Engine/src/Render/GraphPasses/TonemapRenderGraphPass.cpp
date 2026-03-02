#include "Engine/Render/GraphPasses/TonemapRenderGraphPass.hpp"

#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Assets/AssetsLoader.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
struct TonemapPC
{
    glm::vec4 tonemapParams; // x=exposure, y=gamma, z=saturation, w=contrast
    glm::vec4 gradeParams;   // x=temperature, y=tint, z=colorGradingEnabled, w=lutEnabled
    glm::vec4 lutParams;     // x=lutStrength
};

Texture::SharedPtr createIdentityLUTTexture(uint32_t dimension)
{
    if (dimension < 2u)
        dimension = 2u;

    const uint32_t width = dimension * dimension;
    const uint32_t height = dimension;
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 255u);

    for (uint32_t b = 0; b < dimension; ++b)
    {
        for (uint32_t g = 0; g < dimension; ++g)
        {
            for (uint32_t r = 0; r < dimension; ++r)
            {
                const uint32_t x = b * dimension + r;
                const uint32_t y = g;
                const size_t index = (static_cast<size_t>(y) * width + x) * 4u;

                const float rf = static_cast<float>(r) / static_cast<float>(dimension - 1u);
                const float gf = static_cast<float>(g) / static_cast<float>(dimension - 1u);
                const float bf = static_cast<float>(b) / static_cast<float>(dimension - 1u);

                pixels[index + 0] = static_cast<uint8_t>(glm::clamp(rf, 0.0f, 1.0f) * 255.0f + 0.5f);
                pixels[index + 1] = static_cast<uint8_t>(glm::clamp(gf, 0.0f, 1.0f) * 255.0f + 0.5f);
                pixels[index + 2] = static_cast<uint8_t>(glm::clamp(bf, 0.0f, 1.0f) * 255.0f + 0.5f);
                pixels[index + 3] = 255u;
            }
        }
    }

    auto lutTexture = std::make_shared<Texture>();
    if (!lutTexture->createFromMemory(pixels.data(), pixels.size(), width, height, VK_FORMAT_R8G8B8A8_UNORM, 4u))
        return nullptr;

    return lutTexture;
}
} // namespace

TonemapRenderGraphPass::TonemapRenderGraphPass(std::vector<RGPResourceHandler> &hdrInputHandlers) : m_hdrInputHandlers(hdrInputHandlers)
{
    setDebugName("Tonemap render graph pass");

    m_clearValues[0].color = {0.f, 0.f, 0.f, 1.f};

    auto extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();
    setExtent(extent);
}

void TonemapRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_ldrFormat = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();

    for (auto &hdr : m_hdrInputHandlers)
        builder.read(hdr, engine::renderGraph::RGPTextureUsage::SAMPLED);

    engine::renderGraph::RGPTextureDescription ldrDesc{m_ldrFormat, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT};
    ldrDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    ldrDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ldrDesc.setCustomExtentFunction([this]
                                    { return m_extent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        ldrDesc.setDebugName("__ELIX_SCENE_LDR_" + std::to_string(i) + "__");
        auto h = builder.createTexture(ldrDesc);
        m_colorTextureHandler.push_back(h);
        builder.write(h, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
    }

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding hdrBinding{};
    hdrBinding.binding = 0;
    hdrBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    hdrBinding.descriptorCount = 1;
    hdrBinding.pImmutableSamplers = nullptr;
    hdrBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding lutBinding{};
    lutBinding.binding = 1;
    lutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    lutBinding.descriptorCount = 1;
    lutBinding.pImmutableSamplers = nullptr;
    lutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(device, std::vector<VkDescriptorSetLayoutBinding>{hdrBinding, lutBinding});

    m_pipelineLayout = core::PipelineLayout::createShared(device,
                                                          std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout},
                                                          std::vector<VkPushConstantRange>{PushConstant<TonemapPC>::getRange(VK_SHADER_STAGE_FRAGMENT_BIT)});
    m_defaultSampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    if (!m_identityLUTTexture)
        m_identityLUTTexture = createIdentityLUTTexture(16u);

    m_lutDescriptorDirty = true;
}

void TonemapRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                                    const RenderGraphPassContext &renderContext)
{
    const auto &settings = RenderQualitySettings::getInstance();

    const std::string desiredLUTPath = settings.enableLUTGrading ? settings.lutGradingPath : std::string{};
    if (desiredLUTPath != m_lastLUTPath)
    {
        m_lastLUTPath = desiredLUTPath;
        if (!desiredLUTPath.empty())
            m_lutTexture = AssetsLoader::loadTextureGPU(desiredLUTPath, VK_FORMAT_R8G8B8A8_UNORM);
        else
            m_lutTexture.reset();

        m_lutDescriptorDirty = true;
    }

    Texture::SharedPtr activeLUT = m_lutTexture ? m_lutTexture : m_identityLUTTexture;
    if (!activeLUT)
        activeLUT = m_identityLUTTexture = createIdentityLUTTexture(16u);

    if (m_lutDescriptorDirty && m_descriptorSetsInitialized &&
        m_descriptorSets.size() == m_hdrInputTargets.size() && activeLUT)
    {
        auto device = core::VulkanContext::getContext()->getDevice();
        for (size_t i = 0; i < m_descriptorSets.size(); ++i)
        {
            DescriptorSetBuilder::begin()
                .addImage(m_hdrInputTargets[i]->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(activeLUT->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .update(device, m_descriptorSets[i]);
        }
        m_lutDescriptorDirty = false;
    }

    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    GraphicsPipelineKey key{};
    key.shader = ShaderId::ToneMap;
    key.cull = CullMode::None;
    key.depthTest = false;
    key.depthWrite = false;
    key.depthCompare = VK_COMPARE_OP_LESS;
    key.polygonMode = VK_POLYGON_MODE_FILL;
    key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats = {m_ldrFormat};
    key.pipelineLayout = m_pipelineLayout;

    auto graphicsPipeline = GraphicsPipelineManager::getOrCreate(key);

    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    TonemapPC pc{};
    pc.tonemapParams = glm::vec4(
        1.0f,
        2.2f,
        settings.colorGradingSaturation,
        settings.colorGradingContrast);
    pc.gradeParams = glm::vec4(
        settings.colorGradingTemperature,
        settings.colorGradingTint,
        (settings.enablePostProcessing && settings.enableColorGrading) ? 1.0f : 0.0f,
        (settings.enablePostProcessing && settings.enableLUTGrading && activeLUT) ? 1.0f : 0.0f);
    pc.lutParams = glm::vec4(
        glm::clamp(settings.lutGradingStrength, 0.0f, 1.0f),
        0.0f,
        0.0f,
        0.0f);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[renderContext.currentImageIndex], 0, nullptr);
    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<IRenderGraphPass::RenderPassExecution> TonemapRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
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
    exec.colorFormats = {m_ldrFormat};
    exec.depthFormat = VK_FORMAT_UNDEFINED;

    exec.targets[m_colorTextureHandler[renderContext.currentImageIndex]] =
        m_colorRenderTargets[renderContext.currentImageIndex];

    return {exec};
}

void TonemapRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    m_scissor = {{0, 0}, extent};
    requestRecompilation();
}

void TonemapRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    m_colorRenderTargets.resize(imageCount);
    m_hdrInputTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    Texture::SharedPtr activeLUT = m_lutTexture ? m_lutTexture : m_identityLUTTexture;
    if (!activeLUT)
        activeLUT = m_identityLUTTexture = createIdentityLUTTexture(16u);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_colorRenderTargets[i] = storage.getTexture(m_colorTextureHandler[i]);
        m_hdrInputTargets[i] = storage.getTexture(m_hdrInputHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(m_hdrInputTargets[i]->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .addImage(activeLUT->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                                      .build(core::VulkanContext::getContext()->getDevice(), core::VulkanContext::getContext()->getPersistentDescriptorPool(), m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(m_hdrInputTargets[i]->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(activeLUT->vkImageView(), m_defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;

    m_lutDescriptorDirty = false;
}

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END
