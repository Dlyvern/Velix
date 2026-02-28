#include "Engine/Render/GraphPasses/SSAORenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/PushConstant.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <random>
#include <array>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
    struct SSAOpc
    {
        glm::mat4 projection;
        glm::mat4 invProjection;
        glm::vec2 texelSize;
        float radius;
        float bias;
        float strength;
        float enabled; // 0 = pass-through (output 1.0)
        int samples;
        float _pad;
    };

    constexpr int MAX_SSAO_KERNEL = 64;
    struct SSAOKernelUBO
    {
        glm::vec4 samples[MAX_SSAO_KERNEL];
    };

    std::vector<glm::vec4> generateSSAOKernel(int count)
    {
        std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
        std::default_random_engine generator(42);

        std::vector<glm::vec4> kernel;
        kernel.reserve(count);
        for (int i = 0; i < count; ++i)
        {
            glm::vec3 sample(
                randomFloats(generator) * 2.0f - 1.0f,
                randomFloats(generator) * 2.0f - 1.0f,
                randomFloats(generator));
            sample = glm::normalize(sample);
            sample *= randomFloats(generator);

            // Accelerating interpolation — more samples closer to origin
            float scale = static_cast<float>(i) / static_cast<float>(count);
            scale = glm::mix(0.1f, 1.0f, scale * scale);
            sample *= scale;

            kernel.push_back(glm::vec4(sample, 0.0f));
        }
        return kernel;
    }
}

SSAORenderGraphPass::SSAORenderGraphPass(RGPResourceHandler &depthHandler,
                                         std::vector<RGPResourceHandler> &normalHandlers)
    : m_depthHandler(depthHandler), m_normalHandlers(normalHandlers)
{
    setDebugName("SSAO render graph pass");
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void SSAORenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    m_format = VK_FORMAT_R8_UNORM;

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

        outDesc.setDebugName("__ELIX_SSAO_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT);
    }

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

    VkDescriptorSetLayoutBinding kernelBinding{};
    kernelBinding.binding = 2;
    kernelBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    kernelBinding.descriptorCount = 1;
    kernelBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{normalBinding, depthBinding, kernelBinding});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<SSAOpc>::getRange(VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_sampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                            VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    auto kernel = generateSSAOKernel(MAX_SSAO_KERNEL);
    SSAOKernelUBO uboData{};
    for (int i = 0; i < MAX_SSAO_KERNEL && i < static_cast<int>(kernel.size()); ++i)
        uboData.samples[i] = kernel[i];

    m_kernelBuffer = core::Buffer::createShared(sizeof(SSAOKernelUBO),
                                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                core::memory::MemoryUsage::CPU_TO_GPU);
    m_kernelBuffer->map(m_kernelMapped);
    std::memcpy(m_kernelMapped, &uboData, sizeof(SSAOKernelUBO));
}

void SSAORenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputTargets.resize(imageCount);
    m_descriptorSets.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputTargets[i] = storage.getTexture(m_outputHandlers[i]);

        auto normalTex = storage.getTexture(m_normalHandlers[i]);
        auto depthTex = storage.getTexture(m_depthHandler);

        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addImage(normalTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                      .addImage(depthTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                                      .addBuffer(m_kernelBuffer, sizeof(SSAOKernelUBO), 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                      .build(core::VulkanContext::getContext()->getDevice(),
                                             core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                             m_descriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(normalTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .addImage(depthTex->vkImageView(), m_sampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                .addBuffer(m_kernelBuffer, sizeof(SSAOKernelUBO), 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .update(core::VulkanContext::getContext()->getDevice(), m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void SSAORenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                 const RenderGraphPassPerFrameData &data,
                                 const RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    GraphicsPipelineKey key{};
    key.shader = ShaderId::SSAO;
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

    const auto &settings = RenderQualitySettings::getInstance();
    SSAOpc pc{};
    pc.projection = data.projection;
    pc.invProjection = glm::inverse(data.projection);
    pc.texelSize = {1.0f / m_extent.width, 1.0f / m_extent.height};
    pc.radius = settings.ssaoRadius;
    pc.bias = settings.ssaoBias;
    pc.strength = settings.ssaoStrength;
    pc.enabled = (settings.enableSSAO && settings.enablePostProcessing) ? 1.0f : 0.0f;
    pc.samples = glm::clamp(settings.ssaoSamples, 4, MAX_SSAO_KERNEL);

    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

std::vector<IRenderGraphPass::RenderPassExecution>
SSAORenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_outputTargets[renderContext.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = {.color = {1.0f, 1.0f, 1.0f, 1.0f}}; // white = no occlusion

    exec.colorsRenderingItems = {color};
    exec.useDepth = false;
    exec.colorFormats = {m_format};
    exec.depthFormat = VK_FORMAT_UNDEFINED;

    exec.targets[m_outputHandlers[renderContext.currentImageIndex]] =
        m_outputTargets[renderContext.currentImageIndex];

    return {exec};
}

void SSAORenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    m_scissor = {{0, 0}, extent};
    requestRecompilation();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
