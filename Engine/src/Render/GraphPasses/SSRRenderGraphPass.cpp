#include "Engine/Render/GraphPasses/SSRRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/PushConstant.hpp"

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
    struct SSRPC
    {
        glm::mat4 projection;
        glm::mat4 invProjection;
        glm::mat4 invView;
        glm::vec4 params0; // xy=texelSize, z=maxDistance, w=thickness
        glm::vec4 params1; // x=steps, y=strength, z=roughnessCutoff, w=enabled
        glm::vec4 environmentInfo; // x = hasEnvironmentMap
    };
}

SSRRenderGraphPass::SSRRenderGraphPass(std::vector<RGPResourceHandler> &litColorHandlers,
                                       std::vector<RGPResourceHandler> &normalHandlers,
                                       RGPResourceHandler &depthHandler,
                                       std::vector<RGPResourceHandler> &materialHandlers)
    : m_litColorHandlers(litColorHandlers), m_normalHandlers(normalHandlers), m_depthHandler(depthHandler), m_materialHandlers(materialHandlers)
{
    setDebugName("SSR render graph pass");
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    outputs.color.setOwner(this);
}

void SSRRenderGraphPass::prepareRecord(const RenderGraphPassPerFrameData &data,
                                       const RenderGraphPassContext &)
{
    if (m_requestedSkyboxHDRPath != data.skyboxHDRPath)
    {
        m_requestedSkyboxHDRPath = data.skyboxHDRPath;
        m_pendingSkyboxUpdate = true;
        requestRecompilation();
    }
}

bool SSRRenderGraphPass::isEnabled() const
{
    const auto &s = RenderQualitySettings::getInstance();
    return s.enablePostProcessing && s.enableSSR;
}

void SSRRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    m_outputHandlers.clear();

    builder.read(m_depthHandler, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this]
                                    { return m_extent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        builder.read(m_litColorHandlers[i], RGPTextureUsage::SAMPLED);
        builder.read(m_normalHandlers[i], RGPTextureUsage::SAMPLED);
        builder.read(m_materialHandlers[i], RGPTextureUsage::SAMPLED);

        outDesc.setDebugName("__ELIX_SSR_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT);
    }
    outputs.color.set(m_outputHandlers);

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

    VkDescriptorSetLayoutBinding materialBinding{};
    materialBinding.binding = 2;
    materialBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    materialBinding.descriptorCount = 1;
    materialBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding litColorBinding{};
    litColorBinding.binding = 3;
    litColorBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    litColorBinding.descriptorCount = 1;
    litColorBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding environmentBinding{};
    environmentBinding.binding = 4;
    environmentBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    environmentBinding.descriptorCount = 1;
    environmentBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{normalBinding, depthBinding, materialBinding, litColorBinding, environmentBinding});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<SSRPC>::getRange(VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                            VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_depthSampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                 VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void SSRRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    ensureFallbackEnvironmentTexture();
    updateEnvironmentSkybox();

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    const VkImageView environmentImageView =
        (m_environmentSkybox && m_environmentSkybox->hasTexture())
            ? m_environmentSkybox->getEnvImageView()
            : (m_fallbackEnvironmentTexture ? m_fallbackEnvironmentTexture->vkImageView() : VK_NULL_HANDLE);
    const VkSampler environmentSampler =
        (m_environmentSkybox && m_environmentSkybox->hasTexture())
            ? m_environmentSkybox->getEnvSampler()
            : (m_fallbackEnvironmentTexture ? m_fallbackEnvironmentTexture->vkSampler() : VK_NULL_HANDLE);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputTargets[i] = storage.getTexture(m_outputHandlers[i]);

        auto normalTex = storage.getTexture(m_normalHandlers[i]);
        auto depthTex = storage.getTexture(m_depthHandler);
        auto materialTex = storage.getTexture(m_materialHandlers[i]);
        auto litColorTex = storage.getTexture(m_litColorHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(normalTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(depthTex->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                .addImage(materialTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                .addImage(litColorTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 3)
                .addImage(environmentImageView, environmentSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 4)
                .build(core::VulkanContext::getContext()->getDevice(),
                       core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                       m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(normalTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(depthTex->vkImageView(), m_depthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                .addImage(materialTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                .addImage(litColorTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 3)
                .addImage(environmentImageView, environmentSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 4)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void SSRRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                const RenderGraphPassPerFrameData &data,
                                const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    GraphicsPipelineKey key{};
    key.shader = ShaderId::SSR;
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
                            m_pipelineLayout, 0, 1,
                            &m_descriptorSets[renderContext.currentImageIndex], 0, nullptr);

    const auto &s = RenderQualitySettings::getInstance();
    SSRPC pc{};
    pc.projection = data.projection;
    pc.invProjection = glm::inverse(data.projection);
    pc.invView = glm::inverse(data.view);
    pc.params0 = glm::vec4(
        1.0f / static_cast<float>(m_extent.width),
        1.0f / static_cast<float>(m_extent.height),
        s.ssrMaxDistance,
        s.ssrThickness);
    pc.params1 = glm::vec4(
        static_cast<float>(glm::clamp(s.ssrSteps, 8, 256)),
        s.ssrStrength,
        s.ssrRoughnessCutoff,
        (s.enableSSR && s.enablePostProcessing) ? 1.0f : 0.0f);
    pc.environmentInfo = glm::vec4(
        (m_environmentSkybox && m_environmentSkybox->hasTexture()) ? 1.0f : 0.0f,
        0.0f, 0.0f, 0.0f);

    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<IRenderGraphPass::RenderPassExecution>
SSRRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_outputTargets[renderContext.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = {.color = {0.0f, 0.0f, 0.0f, 0.0f}};

    exec.colorsRenderingItems = {color};
    exec.useDepth = false;
    exec.colorFormats = {m_format};
    exec.depthFormat = VK_FORMAT_UNDEFINED;

    exec.targets[m_outputHandlers[renderContext.currentImageIndex]] =
        m_outputTargets[renderContext.currentImageIndex];

    return {exec};
}

void SSRRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    m_scissor = {{0, 0}, extent};
    requestRecompilation();
}

void SSRRenderGraphPass::freeResources()
{
    m_outputHandlers.clear();
    m_outputTargets.clear();
    for (auto &s : m_descriptorSets)
        s = VK_NULL_HANDLE;
    m_descriptorSetsInitialized = false;
    outputs.color.set(MultiHandle{});
}

void SSRRenderGraphPass::cleanup()
{
    m_environmentSkybox.reset();
    m_fallbackEnvironmentTexture.reset();
    m_requestedSkyboxHDRPath.clear();
    m_loadedSkyboxHDRPath.clear();
    m_pendingSkyboxUpdate = true;
}

void SSRRenderGraphPass::ensureFallbackEnvironmentTexture()
{
    if (m_fallbackEnvironmentTexture)
        return;

    m_fallbackEnvironmentTexture = std::make_shared<Texture>();

    constexpr int width = 4;
    constexpr int height = 2;
    const float equirectangularData[width * height * 3] = {
        0.78f, 0.80f, 0.84f,
        0.78f, 0.80f, 0.84f,
        0.78f, 0.80f, 0.84f,
        0.78f, 0.80f, 0.84f,
        0.42f, 0.44f, 0.48f,
        0.42f, 0.44f, 0.48f,
        0.42f, 0.44f, 0.48f,
        0.42f, 0.44f, 0.48f,
    };

    if (!m_fallbackEnvironmentTexture->createCubemapFromEquirectangular(equirectangularData, width, height, 16u))
        throw std::runtime_error("Failed to create fallback SSR environment texture");
}

void SSRRenderGraphPass::updateEnvironmentSkybox()
{
    if (!m_pendingSkyboxUpdate)
        return;

    m_pendingSkyboxUpdate = false;

    if (m_requestedSkyboxHDRPath == m_loadedSkyboxHDRPath)
        return;

    if (m_requestedSkyboxHDRPath.empty())
    {
        m_environmentSkybox.reset();
        m_loadedSkyboxHDRPath.clear();
        return;
    }

    if (!std::filesystem::exists(m_requestedSkyboxHDRPath))
    {
        VX_ENGINE_WARNING_STREAM("SSR skybox HDR file was not found: " << m_requestedSkyboxHDRPath << '\n');
        m_environmentSkybox.reset();
        m_loadedSkyboxHDRPath.clear();
        return;
    }

    auto candidate = std::make_unique<Skybox>(
        m_requestedSkyboxHDRPath,
        core::VulkanContext::getContext()->getPersistentDescriptorPool()->vk());

    if (!candidate->hasTexture())
    {
        VX_ENGINE_WARNING_STREAM("SSR failed to load environment map: " << m_requestedSkyboxHDRPath
                                                                        << ". Falling back to procedural sky.\n");
        m_environmentSkybox.reset();
        m_loadedSkyboxHDRPath.clear();
        return;
    }

    m_environmentSkybox = std::move(candidate);
    m_loadedSkyboxHDRPath = m_requestedSkyboxHDRPath;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
