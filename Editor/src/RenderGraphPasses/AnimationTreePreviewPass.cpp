#include "Editor/RenderGraphPasses/AnimationTreePreviewPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Builders/GraphicsPipelineBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"

#include <glm/glm.hpp>
#include <cstring>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

namespace
{
    struct ModelOnly
    {
        glm::mat4 model{1.0f};
        uint32_t objectId{0u};
        uint32_t padding[3]{0u, 0u, 0u};
    };
}

AnimationTreePreviewPass::AnimationTreePreviewPass()
{
    m_viewport    = VkViewport{0.0f, 0.0f, static_cast<float>(PREVIEW_SIZE), static_cast<float>(PREVIEW_SIZE), 0.0f, 1.0f};
    m_scissor     = VkRect2D{{0, 0}, {PREVIEW_SIZE, PREVIEW_SIZE}};
    m_clearValues[0].color = {0.12f, 0.12f, 0.12f, 1.0f};

    setDebugName("Animation tree preview pass");
}

void AnimationTreePreviewPass::setup(engine::renderGraph::RGPResourcesBuilder &builder)
{
    const auto device = core::VulkanContext::getContext()->getDevice();
    const VkExtent2D extent{PREVIEW_SIZE, PREVIEW_SIZE};

    // Color output
    engine::renderGraph::RGPTextureDescription colorDesc(VK_FORMAT_R8G8B8A8_SRGB, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
    colorDesc.setExtent(extent);
    colorDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    colorDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    colorDesc.setAliasable(false);
    colorDesc.setDebugName("__ELIX_ANIMTREE_PREVIEW_COLOR__");
    m_colorHandler = builder.createTexture(colorDesc);

    // Depth buffer
    engine::renderGraph::RGPTextureDescription depthDesc(VK_FORMAT_D32_SFLOAT, engine::renderGraph::RGPTextureUsage::DEPTH_STENCIL);
    depthDesc.setExtent(extent);
    depthDesc.setInitialLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    depthDesc.setFinalLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    depthDesc.setAliasable(false);
    depthDesc.setDebugName("__ELIX_ANIMTREE_PREVIEW_DEPTH__");
    m_depthHandler = builder.createTexture(depthDesc);

    // Camera descriptor set layout for the isolated preview camera.
    VkDescriptorSetLayoutBinding cameraBinding{};
    cameraBinding.binding         = 0;
    cameraBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraBinding.descriptorCount = 1;
    cameraBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
            *engine::EngineShaderFamilies::cameraDescriptorSetLayout,
            *engine::EngineShaderFamilies::materialDescriptorSetLayout},
        std::vector<VkPushConstantRange>{
            engine::PushConstant<ModelOnly>::getRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                            VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    // Per-frame camera buffers
    const uint32_t frameCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_cameraBuffers.resize(frameCount);
    m_cameraMapped.resize(frameCount, nullptr);

    for (uint32_t i = 0; i < frameCount; ++i)
    {
        m_cameraBuffers[i] = core::Buffer::createShared(sizeof(PreviewCameraUBO),
                                                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                         core::memory::MemoryUsage::CPU_TO_GPU);
        m_cameraBuffers[i]->map(m_cameraMapped[i]);
    }
}

void AnimationTreePreviewPass::compile(engine::renderGraph::RGPResourcesStorage &storage)
{
    m_colorTarget = storage.getTexture(m_colorHandler);
    m_depthTarget = storage.getTexture(m_depthHandler);

    const uint32_t frameCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_descriptorSets.resize(frameCount);

    const auto device = core::VulkanContext::getContext()->getDevice();
    const auto pool   = core::VulkanContext::getContext()->getPersistentDescriptorPool();

    for (uint32_t i = 0; i < frameCount; ++i)
    {
        if (!m_descriptorSetsInitialized)
        {
            m_descriptorSets[i] = engine::DescriptorSetBuilder::begin()
                                       .addBuffer(m_cameraBuffers[i], sizeof(PreviewCameraUBO), 0,
                                                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                       .build(device, pool, engine::EngineShaderFamilies::cameraDescriptorSetLayout);
        }
        else
        {
            engine::DescriptorSetBuilder::begin()
                .addBuffer(m_cameraBuffers[i], sizeof(PreviewCameraUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .update(device, m_descriptorSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void AnimationTreePreviewPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                      const engine::RenderGraphPassPerFrameData &data,
                                      const engine::RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    if (!m_pendingData.hasData || m_pendingData.meshes.empty())
        return;

    // Update camera UBO
    PreviewCameraUBO camData{};
    camData.view = m_pendingData.viewMatrix;
    camData.proj = m_pendingData.projMatrix;
    camData.invView = glm::inverse(camData.view);
    camData.invProj = glm::inverse(camData.proj);
    std::memcpy(m_cameraMapped[renderContext.currentFrame], &camData, sizeof(PreviewCameraUBO));

    engine::GraphicsPipelineKey key{};
    key.shader       = engine::ShaderId::PreviewMesh;
    key.cull         = engine::CullMode::Back;
    key.depthTest    = true;
    key.depthWrite   = true;
    key.depthCompare = VK_COMPARE_OP_LESS;
    key.polygonMode  = VK_POLYGON_MODE_FILL;
    key.topology     = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats = {VK_FORMAT_R8G8B8A8_SRGB};
    key.depthFormat  = VK_FORMAT_D32_SFLOAT;
    key.pipelineLayout = m_pipelineLayout;

    auto pipeline = engine::GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    for (size_t i = 0; i < m_pendingData.meshes.size(); ++i)
    {
        auto *mesh = m_pendingData.meshes[i];
        if (!mesh)
            continue;

        VkBuffer     vertexBuf[] = {mesh->vertexBuffer};
        VkDeviceSize offset[]    = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuf, offset);
        vkCmdBindIndexBuffer(commandBuffer, mesh->indexBuffer, 0, mesh->indexType);

        const glm::mat4 model =
            (i < m_pendingData.meshModelMatrices.size()) ? m_pendingData.meshModelMatrices[i]
                                                         : m_pendingData.modelMatrix;

        ModelOnly pushConstant{};
        pushConstant.model = model;
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(ModelOnly), &pushConstant);

        auto *mat = (i < m_pendingData.materials.size()) ? m_pendingData.materials[i] : nullptr;
        VkDescriptorSet matDS = mat
                                    ? mat->getDescriptorSet(renderContext.currentFrame)
                                    : engine::Material::getDefaultMaterial()->getDescriptorSet(renderContext.currentFrame);

        std::array<VkDescriptorSet, 2> sets{m_descriptorSets[renderContext.currentFrame], matDS};
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                0, static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

        engine::renderGraph::profiling::cmdDrawIndexed(commandBuffer, mesh->indicesCount, 1, 0, 0, 0);
    }
}

std::vector<engine::renderGraph::IRenderGraphPass::RenderPassExecution>
AnimationTreePreviewPass::getRenderPassExecutions(const engine::RenderGraphPassContext &) const
{
    RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = {PREVIEW_SIZE, PREVIEW_SIZE};

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView   = m_colorTarget->vkImageView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue  = m_clearValues[0];
    exec.colorsRenderingItems   = {colorAttachment};

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView             = m_depthTarget->vkImageView();
    depthAttachment.imageLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp                = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp               = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};
    exec.depthRenderingItem = depthAttachment;
    exec.useDepth           = true;

    exec.colorFormats = {VK_FORMAT_R8G8B8A8_SRGB};
    exec.depthFormat  = VK_FORMAT_D32_SFLOAT;

    exec.targets[m_colorHandler] = m_colorTarget;
    exec.targets[m_depthHandler] = m_depthTarget;

    return {exec};
}

VkImageView AnimationTreePreviewPass::getOutputImageView() const
{
    return m_colorTarget ? m_colorTarget->vkImageView() : VK_NULL_HANDLE;
}

VkSampler AnimationTreePreviewPass::getOutputSampler() const
{
    return m_sampler ? m_sampler->vk() : VK_NULL_HANDLE;
}

ELIX_NESTED_NAMESPACE_END
